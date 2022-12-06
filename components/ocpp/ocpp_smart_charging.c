#include <limits.h>
#include <dirent.h>

#include "esp_log.h"

#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "esp_crc.h"

#include "ocpp_smart_charging.h"

#include "ocpp_task.h"
#include "ocpp_listener.h"
#include "types/ocpp_charging_profile_status.h"
#include "types/ocpp_clear_charging_profile_status.h"
#include "types/ocpp_get_composite_schedule_status.h"
#include "types/ocpp_enum.h"
#include "types/ocpp_csl.h"
#include "ocpp_json/ocppj_validation.h"
#include "messages/result_messages/ocpp_call_result.h"
#include "messages/error_messages/ocpp_call_error.h"

/*
 * NOTE: this include REQUIRES zaptec_cloud in the CMake
 */
#include "wpa_supplicant/base64.h"

static const char * TAG = "OCPP SMART     ";
static const char * base_path = "/ocpp"; // TODO: check if too long

static bool mounted = false;

static wl_handle_t s_wl_handle = WL_INVALID_HANDLE;

static SemaphoreHandle_t file_lock = NULL;

static size_t conf_connector_count;
static int conf_max_stack_level;
static char conf_allowed_charging_rate_unit[4];
static int conf_max_periods;
static int conf_max_charging_profiles;

static TaskHandle_t ocpp_smart_task_handle;

void (* charge_value_cb)(float min_charging_limit, float max_charging_limit, uint8_t number_phases) = NULL;
/**
 * Events that  may effect ocpp_smart_task() via xTaskNotify.
 *
 * Not all events are expected to be sendt as xTaskNotify (but may). Some are set as needed by ocpp_smart_task() itself
 * to make use of more bits in the ulValue (uint32_t) parameter to xTaskNotify.
 */
enum ocpp_smart_event{

	// 0-2 used/reserved for task/thread management
	eNOT_NEEDED = 1<<0, // Requested to stop

	// 3-8 used/reserved for profiles and schedules
	eACTIVE_PROFILE_TX_CHANGE = 1<<3,
	eACTIVE_PROFILE_MAX_CHANGE = 1<<4,
	eSCHEDULE_CHANGE = 1<<5,

	// 9-10 used for periods
	eACTIVE_PERIOD_CHANGE = 1<<9,

	// 11-15 used for transaction events
	eTRANSACTION_START = 1<<11,
	eTRANSACTION_STOP = 1<<12,
	eTRANSACTION_ID_CHANGED = 1<<13,

	// 16-20 used for ocpp message events
	eNEW_PROFILE_TX = 1<<16,
	eNEW_PROFILE_MAX = 1<<17
};

struct ocpp_charging_profile ** tx_profiles = NULL;

// Calling this function transfers the responsibility of freeing memory
esp_err_t update_charging_profile(struct ocpp_charging_profile * profile){
	ESP_LOGI(TAG, "Updating charging profile");

	if(!mounted){
		ESP_LOGE(TAG, "Update failed: Not mounted");
		ocpp_free_charging_profile(profile);
		return ESP_ERR_INVALID_STATE;
	}

	if(xSemaphoreTake(file_lock, pdMS_TO_TICKS(2000)) != pdTRUE){
		ESP_LOGE(TAG, "Update failed: Mutex not aquired");
		ocpp_free_charging_profile(profile);
		return ESP_ERR_TIMEOUT;
	}

	char profile_directory[4];
	switch(profile->profile_purpose){
	case eOCPP_CHARGING_PROFILE_PURPOSE_CHARGE_POINT_MAX:
		strcpy(profile_directory, "max");
		break;
	case eOCPP_CHARGING_PROFILE_PURPOSE_TX_DEFAULT:
		strcpy(profile_directory, "tx");
		break;
	case eOCPP_CHARGING_PROFILE_PURPOSE_TX:
		profile_directory[0] = '\0';
		break;
	}

	if(profile_directory[0] == '\0'){

		struct ocpp_charging_profile * before = tx_profiles[profile->stack_level];

		if(before != NULL){
			ocpp_free_charging_profile(before);

		}
		tx_profiles[profile->stack_level] = profile;

		xSemaphoreGive(file_lock);
		return ESP_OK;
	}

	char profile_path[32];
	sprintf(profile_path, "%s/%s_%d.bin", base_path, profile_directory, profile->stack_level);

	errno = 0;
	FILE * fp = fopen(profile_path, "wb");
	if(fp == NULL){
		ESP_LOGE(TAG, "Unable to open file '%s': %s", profile_path, strerror(errno));
		goto error;
	}else{
		ESP_LOGI(TAG, "Opened or Created '%s' for writing charging profile", profile_path);
	}

	uint8_t version = 1;
	if(fwrite(&version, sizeof(uint8_t), 1, fp) != 1){
		ESP_LOGE(TAG, "Unable to write file version");
		goto error;
	}

	/*
	 * We create a base64 string from the profile with pointers to create a crc for
	 * the majority of the data. The values the pointers point to are then appended after the
	 * crc on file. The pointers will then be used during the read to find which values are null
	 * and which values needs to be read. The pointer itself will be overwritten during the read.
	 *
	 * NOTE: transaction_id is always NULL as it is only used for tx profiles (not tx default nor max).
	 */
	size_t out_length;
	char * base64_profile = base64_encode(profile, sizeof(struct ocpp_charging_profile), &out_length);
	if(base64_profile == NULL){
		ESP_LOGE(TAG, "Failed to create base64 profile");
		goto error;
	}

	if(fwrite(&out_length, sizeof(size_t), 1, fp) != 1){
		ESP_LOGE(TAG, "Failed to write profile length");
		free(base64_profile);
		goto error;
	}

	if(fwrite(base64_profile, sizeof(char), out_length, fp) != out_length){
		ESP_LOGE(TAG, "Failed to write base64 profile");
		free(base64_profile);
		goto error;
	}

	uint32_t crc_calc = esp_crc32_le(0, (uint8_t *)base64_profile, out_length);
	if(fwrite(&crc_calc, sizeof(uint32_t), 1, fp) != 1){
		ESP_LOGE(TAG, "Unable to write crc");
		goto error;
	}

	free(base64_profile);

	if(profile->transaction_id != NULL){
		if(fwrite(profile->transaction_id, sizeof(int), 1, fp) != 1){
			ESP_LOGE(TAG, "Failed to write transaction_id");
			goto error;
		}
	}
	if(profile->recurrency_kind != NULL){
		uint8_t enum_value = *profile->recurrency_kind;
		if(fwrite(&enum_value, sizeof(uint8_t), 1, fp) != 1){
			ESP_LOGE(TAG, "Failed to write recurrency kind");
			goto error;
		}
	}

	if(profile->charging_schedule.duration != NULL){
		if(fwrite(profile->charging_schedule.duration, sizeof(int), 1, fp) != 1){
			ESP_LOGE(TAG, "Failed to write duration");
			goto error;
		}
	}
	if(profile->charging_schedule.start_schedule != NULL){
		if(fwrite(profile->charging_schedule.start_schedule, sizeof(time_t), 1, fp) != 1){
			ESP_LOGE(TAG, "Failed to write schedule start time");
			goto error;
		}
	}

	/*
	 * We create an other base64 string and crc for the optional periods. The first (mandatory) period will be
	 * part of the base64 profile string written above.
	 */
	size_t optional_period_count = 0;
	struct ocpp_charging_schedule_period_list * optional_period = profile->charging_schedule.schedule_period.next;

	while(optional_period != NULL){
		optional_period_count++;
		optional_period = optional_period->next;
	}

	if(optional_period_count != 0){
		if(fwrite(&optional_period_count, sizeof(size_t), 1, fp) != 1){
			ESP_LOGE(TAG, "Unable to write period count");
			goto error;
		}

		struct ocpp_charging_schedule_period * periods = malloc(sizeof(struct ocpp_charging_schedule_period) * optional_period_count);
		if(periods == NULL){
			ESP_LOGE(TAG, "Unable to allocate memory for periods");
			goto error;
		}

		optional_period = profile->charging_schedule.schedule_period.next;

		for(size_t i = 0; i < optional_period_count; i++){
			memcpy(&periods[i], &optional_period->value, sizeof(struct ocpp_charging_schedule_period));
			optional_period = optional_period->next;
		}

		char * base64_periods = base64_encode(periods, sizeof(struct ocpp_charging_schedule_period) * optional_period_count, &out_length);
		free(periods);

		if(base64_periods == NULL){
			ESP_LOGE(TAG, "Unable to create base64 periods");
			goto error;
		}

		if(fwrite(&out_length, sizeof(size_t), 1, fp) != 1){
			ESP_LOGE(TAG, "Unable to write periods length");
			free(base64_periods);
			goto error;
		}

		crc_calc = esp_crc32_le(0, (uint8_t *)base64_periods, out_length);

		size_t written = fwrite(base64_periods, sizeof(char), out_length, fp);
		free(base64_periods);

		if(written != out_length){
			ESP_LOGE(TAG, "Failed to write optional periods");
			goto error;
		}

		if(fwrite(&crc_calc, sizeof(uint32_t), 1, fp) != 1){
			ESP_LOGE(TAG, "Failed to write crc for periods");
			goto error;
		}

	}

	ESP_LOGI(TAG, "Completed write to '%s'", profile_path);

	fclose(fp);
	xSemaphoreGive(file_lock);

	ocpp_free_charging_profile(profile);

	return ESP_OK;

error:
	if(fp != NULL){
		fclose(fp);
		ESP_LOGE(TAG, "Failure during profile write, Deleting file '%s'", profile_path);

		remove(profile_path);
	}

	xSemaphoreGive(file_lock);

	ocpp_free_charging_profile(profile);

	return ESP_FAIL;
}

enum invalid_ptr {
	eFROM_NULL,
	eFROM_START,
	eFROM_TRANSACTION_ID,
	eFROM_RECURRENCY_KIND,
	eFROM_SCHEDULE_DURATION,
	eFROM_SCHEDULE_START,
	eFROM_PERIOD
};

int remove_profile_from_memory(int * id, int * stack_level, bool imediate){
	ESP_LOGI(TAG, "Removing profile from memory");
	if(stack_level != NULL){
		struct ocpp_charging_profile * old = tx_profiles[*stack_level];

		if(old == NULL || (id != NULL && *id == old->profile_id)){
			return 0;
		}

		if(imediate){
			ocpp_free_charging_profile(tx_profiles[*stack_level]);
			tx_profiles[*stack_level] = NULL;
		}else{
			tx_profiles[*stack_level]->valid_to = 0;
		}
		return 1;

	}else if(id != NULL){
		for(size_t i = 0; i < conf_max_stack_level+1; i++){
			if(tx_profiles[i] != NULL && tx_profiles[i]->profile_id == *id){
				if(imediate){
					ocpp_free_charging_profile(tx_profiles[i]);
					tx_profiles[i] = NULL;
				}else{
					tx_profiles[i]->valid_to = 0;
				}
				return 1;
			}
		}
		return 0;

	}else{
		int removed_count = 0;
		for(size_t i = 0; i < conf_max_stack_level +1; i++){
			if(tx_profiles[i] != NULL){
				if(imediate){
					ocpp_free_charging_profile(tx_profiles[i]);
					tx_profiles[i] = NULL;
				}else{
					tx_profiles[i]->valid_to = 0;
				}
				removed_count++;
			}
		}

		return removed_count;
	}
}

struct ocpp_charging_profile * read_profile_from_file(const char * profile_path){
	FILE * fp = fopen(profile_path, "rb");
	if(fp == NULL){
		ESP_LOGE(TAG, "Unable to open next profile '%s': %s", profile_path, strerror(errno));
		xSemaphoreGive(file_lock);
		return NULL;
	}

	struct ocpp_charging_profile * profile = NULL;

	// Since the pointers are written to file, they need to be removed before the resulting profile
	// is freed in case of an error. failed_from indicate point from which pointers cease to be valid.
	enum invalid_ptr failed_from  = eFROM_NULL;

	uint8_t version;
	if(fread(&version, sizeof(uint8_t), 1, fp) != 1){
		ESP_LOGE(TAG, "Unable to read version");
		goto error;
	}

	size_t out_length;
	if(fread(&out_length, sizeof(size_t), 1, fp) != 1){
		ESP_LOGE(TAG, "Failed to read  profile length");
		goto error;
	}


	char * base64_str = malloc(sizeof(char) * out_length);
	if(base64_str == NULL){
		ESP_LOGE(TAG, "Unable to allocate memory for base64 string of profile");
		goto error;
	}

	if(fread(base64_str, sizeof(char), out_length, fp) != out_length){
		ESP_LOGE(TAG, "Unable to read base64 profile string");
		free(base64_str);
		goto error;
	}

	uint32_t crc;
	if(fread(&crc, sizeof(uint32_t), 1, fp) != 1){
		ESP_LOGE(TAG, "Unable to read crc");
		free(base64_str);
		goto error;
	}

	uint32_t crc_calc = esp_crc32_le(0, (uint8_t *)base64_str, out_length);
	if(crc != crc_calc){
		ESP_LOGE(TAG, "crc did not match");
		goto error;
	}

	size_t decoded_length;
	profile = (struct ocpp_charging_profile *) base64_decode(base64_str, out_length, &decoded_length);
	free(base64_str);

	if(profile == NULL){
		ESP_LOGE(TAG, "Unable to decode base64 profile");
		goto error;
	}

	if(decoded_length != sizeof(struct ocpp_charging_profile)){
		ESP_LOGE(TAG, "Unexpected length of decoded data ");
		goto error;
	}

	if(profile->transaction_id != NULL){
		failed_from = eFROM_TRANSACTION_ID;

		profile->transaction_id = malloc(sizeof(int));
		if(profile->transaction_id == NULL){
			ESP_LOGE(TAG, "Unable to allocate memory for transaction id");
			goto error;
		}
		if(fread(profile->transaction_id, sizeof(int), 1, fp) != 1){
			ESP_LOGE(TAG, "Failed to read transaction id");
			goto error;
		}
	}
	if(profile->recurrency_kind != NULL){
		failed_from = eFROM_RECURRENCY_KIND;

		profile->recurrency_kind = malloc(sizeof(enum ocpp_recurrency_kind));
		if(profile->recurrency_kind == NULL){
			ESP_LOGE(TAG, "Unable to allocate memory for recurrency kind");
			goto error;
		}

		uint8_t enum_value;
		if(fread(&enum_value, sizeof(uint8_t), 1, fp) != 1){
			ESP_LOGE(TAG, "Failed to read recurrency kind");
			goto error;
		}
		*profile->recurrency_kind = enum_value;
	}

	if(profile->charging_schedule.duration != NULL){
		failed_from = eFROM_SCHEDULE_DURATION;

		profile->charging_schedule.duration = malloc(sizeof(int));
		if(profile->charging_schedule.duration == NULL){
			ESP_LOGE(TAG, "Unable to allocate memory for duration");
			goto error;
		}
		if(fread(profile->charging_schedule.duration, sizeof(int), 1, fp) != 1){
			ESP_LOGE(TAG, "Failed to read duration");
			goto error;
		}
	}
	if(profile->charging_schedule.start_schedule != NULL){
		failed_from = eFROM_SCHEDULE_START;

		profile->charging_schedule.start_schedule = malloc(sizeof(time_t));
		if(profile->charging_schedule.start_schedule == NULL){
			ESP_LOGE(TAG, "Unable to allocate memory for schedule start");
			goto error;
		}
		if(fread(profile->charging_schedule.start_schedule, sizeof(time_t), 1, fp) != 1){
			ESP_LOGE(TAG, "Failed to read schedule start");
			goto error;
		}
	}

	if(profile->charging_schedule.schedule_period.next != NULL){
		size_t periods_count;
		if(fread(&periods_count, sizeof(size_t), 1, fp) != 1){
			ESP_LOGE(TAG, "Unable to read periods count");
		}

		size_t out_length;
		if(fread(&out_length, sizeof(size_t), 1, fp) != 1){
			ESP_LOGE(TAG, "Unable to read lenght of the base64 periods string");
		}

		char * base64_periods = malloc(sizeof(char) * out_length);
		if(base64_str == NULL){
			ESP_LOGE(TAG, "Unable to allocate memory for base64 string of periods");
			goto error;
		}
		if(fread(base64_periods, sizeof(char), out_length, fp) != out_length){
			ESP_LOGE(TAG, "Unable to read base64 periods string");
			free(base64_str);
			goto error;
		}

		uint32_t crc;
		if(fread(&crc, sizeof(uint32_t), 1, fp) != 1){
			ESP_LOGE(TAG, "Unable to read crc of periods");
			free(base64_str);
			goto error;
		}

		uint32_t crc_calc = esp_crc32_le(0, (uint8_t *)base64_periods, out_length);
		if(crc != crc_calc){
			ESP_LOGE(TAG, "crc did not match for periods");
			goto error;
		}

		size_t decoded_length;
		struct ocpp_charging_schedule_period * periods =
			(struct ocpp_charging_schedule_period *) base64_decode(base64_periods, out_length,
									&decoded_length);
		free(base64_periods);

		if(periods == NULL){
			ESP_LOGE(TAG, "Unable to create periods from base64 string");
			goto error;
		}

		// TODO: check decoded length compared to length of each expected ocpp_charging_scheduøe_period
		failed_from = eFROM_PERIOD;

		struct ocpp_charging_schedule_period_list ** entry = &profile->charging_schedule.schedule_period.next;
		*entry = NULL;

		for(size_t i = 0; i < periods_count; i++){
			*entry = malloc(sizeof(struct ocpp_charging_schedule_period_list));
			if(entry == NULL){
				ESP_LOGE(TAG, "Unable to allcate memory for new period entry");
			}else{
				(*entry)->next = NULL;
			}

			memcpy(&(*entry)->value, &periods[i], sizeof(struct ocpp_charging_schedule_period));
			entry = &(*entry)->next;
		}
	}

	fclose(fp);
	xSemaphoreGive(file_lock);

	ESP_LOGI(TAG, "Next charge profile aquired");
	return profile;

error:
	switch(failed_from){
	case eFROM_NULL:
		break;
	case eFROM_START:
		profile->transaction_id = NULL;
		/* fall through */
	case eFROM_TRANSACTION_ID:
		profile->recurrency_kind = NULL;
		/* fall through */
	case eFROM_RECURRENCY_KIND:
		profile->charging_schedule.duration = NULL;
		/* fall through */
	case eFROM_SCHEDULE_DURATION:
		profile->charging_schedule.start_schedule = NULL;
		/* fall through */
	case eFROM_SCHEDULE_START:
		profile->charging_schedule.schedule_period.next = NULL;
		/* fall through */
	case eFROM_PERIOD:
		ESP_LOGI(TAG, "Invalid pointers removed");
	}

	ocpp_free_charging_profile(profile);
	fclose(fp);
	ESP_LOGE(TAG, "Removing '%s' schedule due to read error", profile_path);
	remove(profile_path);
	xSemaphoreGive(file_lock);

	return NULL;
}

int remove_profile_on_file(int * id, int * stack_level, enum ocpp_charging_profile_purpose purpose){
	ESP_LOGI(TAG, "Removing profile from file");
	char profile_path[32];
	uint8_t removed_count = 0;

	if(stack_level != NULL){
		if(purpose == eOCPP_CHARGING_PROFILE_PURPOSE_CHARGE_POINT_MAX){
			sprintf(profile_path, "%s/%s_%d.bin", base_path, "max", *stack_level);
		}else if(purpose == eOCPP_CHARGING_PROFILE_PURPOSE_TX_DEFAULT){
			sprintf(profile_path, "%s/%s_%d.bin", base_path, "tx", *stack_level);
		}else{
			ESP_LOGE(TAG, "Invalid profile purpose for removing profile on file");
			return 0;
		}

		if(id != NULL){
			struct ocpp_charging_profile * profile = read_profile_from_file(profile_path);
			if(profile == NULL){
				ESP_LOGE(TAG, "Unable to read profile for removal");
				return 0;
			}else{
				int profile_id = profile->profile_id;
				ocpp_free_charging_profile(profile);

				if(profile_id != *id){
					ESP_LOGW(TAG,  "Profile at given stack level did not have gieven profile id");
					return 0;
				}
			}
		}

		errno = 0;
		int err = remove(profile_path);
		if(err == 0){
			ESP_LOGI(TAG, "Successfully removed profile at '%s'", profile_path);
			return 1;
		}else{
			ESP_LOGE(TAG, "Unable to remove profile at '%s': %s", profile_path, strerror(errno));
			return 0;
		}

	}else{
		const char * name_prefix = (purpose == eOCPP_CHARGING_PROFILE_PURPOSE_CHARGE_POINT_MAX) ? "MAX_" : "TX_";
		DIR * dir = opendir(base_path);
		if(dir == NULL){
			ESP_LOGE(TAG, "Unable to open directory");
			return 0;
		}

		struct dirent * dp = readdir(dir);
		while(dp != NULL){
			if(dp->d_type == DT_REG){
				if(strstr(dp->d_name, name_prefix) == dp->d_name){
					sprintf(profile_path, "%s/%.12s", base_path, dp->d_name);
					ESP_LOGI(TAG, "Selecting file with path: %s", profile_path);

					if(id != NULL){
						ESP_LOGI(TAG, "Opening file to check id");

						struct ocpp_charging_profile * profile = read_profile_from_file(profile_path);
						if(profile != NULL){
							if(profile->profile_id == *id){
								ESP_LOGI(TAG, "Id matched. removing file");

								ocpp_free_charging_profile(profile);

								errno = 0;
								if(remove(profile_path) == 0){
									removed_count++;
									break;
								}else{
									ESP_LOGE(TAG, "Unable to remove matching profile file: %s", strerror(errno));
									break;
								}
							}
							ocpp_free_charging_profile(profile);
						}
					}else{
						ESP_LOGI(TAG, "Removing file");
						errno = 0;
						if(remove(profile_path) == 0){
							 removed_count++;
						}else{
							ESP_LOGE(TAG, "Unable to remove matching profile file: %s", strerror(errno));
						}
					}
				}
			}
			dp = readdir(dir);
		}
		errno = 0;
		if(closedir(dir) != 0){
			ESP_LOGE(TAG, "Failed to close directory: %s", strerror(errno));
		}
	}

	return removed_count;
}

void remove_profile(struct ocpp_charging_profile * profile, bool imediate){
	if(profile == NULL)
		return;

	if(xSemaphoreTake(file_lock, pdMS_TO_TICKS(2000)) != pdTRUE){
		ESP_LOGE(TAG, "Unable to remove profile: Mutex not aquired");
		return;
	}

	switch(profile->profile_purpose){
	case eOCPP_CHARGING_PROFILE_PURPOSE_CHARGE_POINT_MAX:
	case eOCPP_CHARGING_PROFILE_PURPOSE_TX_DEFAULT:
		remove_profile_on_file(NULL, &profile->stack_level, profile->profile_purpose);
		break;
	case eOCPP_CHARGING_PROFILE_PURPOSE_TX:
		remove_profile_from_memory(NULL, &profile->stack_level, imediate);
		break;
	default:
		ESP_LOGE(TAG, "Attempt to remove profile with invalid purpose");
	}

	xSemaphoreGive(file_lock);
}

int remove_matching_profile(int * id,  int * connector_id, enum ocpp_charging_profile_purpose  * purpose, int * stack_level, bool imediate){

	if(xSemaphoreTake(file_lock, pdMS_TO_TICKS(2000)) != pdTRUE){
		ESP_LOGE(TAG, "Unable to remove matching profile: Mutex not aquired");
		return -1;
	}

	int removed_count = 0;
	if(purpose == NULL || *purpose == eOCPP_CHARGING_PROFILE_PURPOSE_TX)
		removed_count += remove_profile_from_memory(id, stack_level, imediate);

	if(purpose == NULL || *purpose == eOCPP_CHARGING_PROFILE_PURPOSE_TX_DEFAULT)
		removed_count += remove_profile_on_file(id, stack_level, eOCPP_CHARGING_PROFILE_PURPOSE_TX_DEFAULT);

	if(purpose == NULL || *purpose == eOCPP_CHARGING_PROFILE_PURPOSE_TX_DEFAULT)
		removed_count += remove_profile_on_file(id, stack_level, eOCPP_CHARGING_PROFILE_PURPOSE_CHARGE_POINT_MAX);

	xSemaphoreGive(file_lock);

	return removed_count;
}

struct ocpp_charging_profile * next_tx_profile(struct ocpp_charging_profile * current_profile){
	if(xSemaphoreTake(file_lock, pdMS_TO_TICKS(2000)) != pdTRUE){
		ESP_LOGE(TAG, "Unable to get next tx profile: Mutex not aquired");
		return NULL;
	}

	int requested_stack_level = (current_profile != NULL) ? current_profile->stack_level-1 : conf_max_stack_level;

	for(; requested_stack_level >= 0; requested_stack_level--){
		if(tx_profiles[requested_stack_level] != NULL){
			xSemaphoreGive(file_lock);
			return tx_profiles[requested_stack_level];
		}
	}
	xSemaphoreGive(file_lock);
	return NULL;
}

struct ocpp_charging_profile * next_charge_profile_from_file(struct ocpp_charging_profile * current_profile,
							enum ocpp_charging_profile_purpose purpose){

	ESP_LOGI(TAG, "Request for next profile.");

	if(purpose != eOCPP_CHARGING_PROFILE_PURPOSE_CHARGE_POINT_MAX && purpose != eOCPP_CHARGING_PROFILE_PURPOSE_TX_DEFAULT){
		ESP_LOGE(TAG, "Only tx default and max profiles are stored on file");
		return NULL;
	}

	if(mounted == false){
		ESP_LOGE(TAG, "Unable to read profile: not mounted");
		return NULL;
	}

	if(xSemaphoreTake(file_lock, pdMS_TO_TICKS(2000)) != pdTRUE){
		ESP_LOGE(TAG, "Unable to get next profile on file: Mutex not aquired");
		return NULL;
	}

	DIR * dir = opendir(base_path);
	if(dir == NULL){
		ESP_LOGE(TAG, "Unable to open directory");
		xSemaphoreGive(file_lock);
		return NULL;
	}

	const char * name_prefix = (purpose == eOCPP_CHARGING_PROFILE_PURPOSE_CHARGE_POINT_MAX) ? "MAX_" : "TX_"; // File names are uppercase
	int best_candidate = -1;
	uint8_t current_stack_level = (current_profile == NULL) ? UINT8_MAX : current_profile->stack_level;

	ESP_LOGI(TAG, "Requested type prefix: %s, current stack lvel: %d", name_prefix, current_stack_level);

	struct dirent * dp = readdir(dir);
	while(dp != NULL){
		if(dp->d_type == DT_REG){
			ESP_LOGI(TAG, "Found file: %s", dp->d_name);
			if(strstr(dp->d_name, name_prefix) == dp->d_name){

				errno = 0;
				long stack_level = strtol(dp->d_name + strlen(name_prefix), NULL, 0);
				if(stack_level == 0 && errno != 0){
					ESP_LOGE(TAG, "Error during strtol for finding next stack level: '%s'", strerror(errno));
					continue;
				}

				if(stack_level > best_candidate && stack_level < current_stack_level){
					ESP_LOGI(TAG, "New best candidate: '%s'", dp->d_name);
					best_candidate = (int) stack_level;

					if(best_candidate == (current_stack_level - 1)) // if best possible candidate
						break;
				}
			}
		}
		dp = readdir(dir);
	}

	closedir(dir);

	if(best_candidate == -1){
		ESP_LOGW(TAG, "Found no next profile on file");
		xSemaphoreGive(file_lock);
		return NULL;
	}

	char profile_path[32];
	sprintf(profile_path, "%s/%s%d.bin", base_path, name_prefix, best_candidate);

	ESP_LOGI(TAG, "Attempting to get best charging profile: %s", profile_path);

	struct ocpp_charging_profile * result = read_profile_from_file(profile_path);

	xSemaphoreGive(file_lock);
	return result;
}

struct ocpp_charging_profile * next_tx_or_tx_default_profile(struct ocpp_charging_profile * current_profile){
	struct ocpp_charging_profile * result = NULL;

	if(current_profile == NULL || current_profile->profile_purpose == eOCPP_CHARGING_PROFILE_PURPOSE_TX){
		result  = next_tx_profile(current_profile);
	}

	if(result != NULL)
		return result;

	result = next_charge_profile_from_file(current_profile, eOCPP_CHARGING_PROFILE_PURPOSE_TX_DEFAULT);

	if(result != NULL)
		return result;

	return ocpp_get_default_charging_profile(eOCPP_CHARGING_PROFILE_PURPOSE_TX_DEFAULT);
}

struct ocpp_charging_profile * next_max_profile(struct ocpp_charging_profile * current_profile){
	struct ocpp_charging_profile * result = next_charge_profile_from_file(current_profile, eOCPP_CHARGING_PROFILE_PURPOSE_CHARGE_POINT_MAX);

	if(result != NULL){
		return result;
	}else{
		return ocpp_get_default_charging_profile(eOCPP_CHARGING_PROFILE_PURPOSE_CHARGE_POINT_MAX);
	}
}

void clear_charging_profile_cb(const char * unique_id, const char * action, cJSON * payload, void * cb_data){
	ESP_LOGI(TAG, "Received request for clear charging profile");

	char err_str[128];

	uint8_t field_mask = 0; // Indicate which fields in the json structure are filled

	int id;
	enum ocppj_err_t err = ocppj_get_int_field(payload, "id", false, &id, err_str, sizeof(err_str));
	if(err == eOCPPJ_NO_ERROR){
		field_mask |= 1<<0;

	}else if(err != eOCPPJ_NO_VALUE){
		ESP_LOGW(TAG, "Invalid clear charging profile request: '%s'", err_str);
		goto error;
	}

	int connector;
	err = ocppj_get_int_field(payload, "connectorId", false, &connector, err_str, sizeof(err_str));
	if(err == eOCPPJ_NO_ERROR){
		field_mask |= 1<<1;
	}else if(err != eOCPPJ_NO_VALUE){
		ESP_LOGW(TAG, "Invalid clear charging profile request: '%s'", err_str);
		goto error;
	}

	char * purpose;
	enum ocpp_charging_profile_purpose purpose_id;
	err = ocppj_get_string_field(payload, "chargingProfilePurpose", false, &purpose, err_str, sizeof(err_str));
	if(err == eOCPPJ_NO_ERROR){

		if(ocpp_validate_enum(purpose, true, 3,
					OCPP_CHARGING_PROFILE_PURPOSE_CHARGE_POINT_MAX,
					OCPP_CHARGING_PROFILE_PURPOSE_TX_DEFAULT,
					OCPP_CHARGING_PROFILE_PURPOSE_TX) == 0){

			purpose_id = ocpp_charging_profile_purpose_to_id(purpose);
			field_mask |= 1<<2;
		}else{
			err = eOCPPJ_ERROR_TYPE_CONSTRAINT_VIOLATION;
			snprintf(err_str, sizeof(err_str), "Expected chargingProfilePurpose to be a valid ChargingProfilePurposeType");
			goto error;
		}
	}else if(err != eOCPPJ_NO_VALUE){
		ESP_LOGW(TAG, "Invalid clear charging profile request: '%s'", err_str);
		goto error;
	}

	int stack_level;
	err = ocppj_get_int_field(payload, "stackLevel", false, &stack_level, err_str, sizeof(err_str));
	if(err == eOCPPJ_NO_ERROR){
		field_mask |= 1<<3;
	}else if(err != eOCPPJ_NO_VALUE){
		ESP_LOGW(TAG, "Invalid clear charging profile request: '%s'", err_str);
		goto error;
	}

	int removed_count = remove_matching_profile(
		(field_mask & 1<<0) ? &id : NULL,
		(field_mask & 1<<1) ? &connector : NULL,
		(field_mask & 1<<2) ? &purpose_id : NULL,
		(field_mask & 1<<3) ? &stack_level : NULL,
		false);

	cJSON * reply;
	if(removed_count > 0){
		reply = ocpp_create_clear_charging_profile_confirmation(unique_id, OCPP_CLEAR_CHARGING_PROFILE_STATUS_ACCEPTED);
		if(field_mask & 1<<2){
			if(purpose_id == eOCPP_CHARGING_PROFILE_PURPOSE_TX_DEFAULT
				|| purpose_id == eOCPP_CHARGING_PROFILE_PURPOSE_TX){

				xTaskNotify(ocpp_smart_task_handle, eNEW_PROFILE_TX, eSetBits);

			}else if(purpose_id == eOCPP_CHARGING_PROFILE_PURPOSE_CHARGE_POINT_MAX){
				xTaskNotify(ocpp_smart_task_handle, eNEW_PROFILE_MAX, eSetBits);
			}
		}else{
			xTaskNotify(ocpp_smart_task_handle, eNEW_PROFILE_MAX | eNEW_PROFILE_TX, eSetBits);
		}
	}else if(removed_count == 0){
		reply = ocpp_create_clear_charging_profile_confirmation(unique_id, OCPP_CLEAR_CHARGING_PROFILE_STATUS_UNKNOWN);
	}else{
		err = eOCPPJ_ERROR_INTERNAL;
		strcpy(err_str, "Error occured during search and removal of profile");
		goto error;
	}

	if(reply != NULL){
		send_call_reply(reply);
		cJSON_Delete(reply);
	}else{
		ESP_LOGE(TAG, "Unable to create ClearChargingProfile.conf");
	}

	return;

error:
	if(err != eOCPPJ_NO_ERROR && eOCPPJ_NO_VALUE){
		cJSON * reply = ocpp_create_call_error(unique_id, ocppj_error_code_from_id(err), err_str, NULL);
		if(reply != NULL){
			send_call_reply(reply);
			cJSON_Delete(reply);
		}
	}else{
		ESP_LOGE(TAG, "Error occured during clear charging profile, but no ocpp json error set");
	}
}

void set_charging_profile_cb(const char * unique_id, const char * action, cJSON * payload, void * cb_data){
	ESP_LOGI(TAG, "Received request to set charging profile");

	int connector_id;
	struct ocpp_charging_profile * charging_profile = calloc(sizeof(struct ocpp_charging_profile), 1);

	cJSON * reply = NULL;

	if(charging_profile == NULL){
		ESP_LOGE(TAG, "Unable to allocate memory for charging profile");
		reply = ocpp_create_call_error(unique_id, OCPPJ_ERROR_INTERNAL, "Unable to allocate memory for charging profile", NULL);
		goto error;
	}
	char err_str[128];

	if(cJSON_HasObjectItem(payload, "connectorId")){
		cJSON * connector_id_json = cJSON_GetObjectItem(payload, "connectorId");
		if(cJSON_IsNumber(connector_id_json)){

			connector_id = connector_id_json->valueint;

			if(connector_id < 0 || connector_id > 1){ // TODO: replace 1 with number of connectors from config
				ESP_LOGW(TAG, "Recieved invalid 'connectorId'");
				reply = ocpp_create_call_error(unique_id, OCPPJ_ERROR_PROPERTY_CONSTRAINT_VIOLATION, "'connectorId' does not name a valid connector", NULL);
				goto error;
			}
		}else{
			ESP_LOGW(TAG, "Recieved 'connectorId' with invalid type");
			reply = ocpp_create_call_error(unique_id, OCPPJ_ERROR_TYPE_CONSTRAINT_VIOLATION, "Expected 'connectorId' to be integer", NULL);
			goto error;
		}
	}else{
		ESP_LOGW(TAG, "Missing 'connectorId'");
		reply = ocpp_create_call_error(unique_id, OCPPJ_ERROR_FORMATION_VIOLATION, "Expected 'connectorId' field", NULL);
		goto error;
	}

	if(cJSON_HasObjectItem(payload, "csChargingProfiles")){

		cJSON * charging_profile_json = cJSON_GetObjectItem(payload, "csChargingProfiles");
		enum ocppj_err_t err =  ocpp_charging_profile_from_json(charging_profile_json, conf_max_stack_level,
								conf_allowed_charging_rate_unit, conf_max_periods,
								charging_profile, err_str, sizeof(err_str));

		if(err != eOCPPJ_NO_ERROR){
			ESP_LOGW(TAG, "Invalid charging profile: %s", err_str);

			reply = ocpp_create_call_error(unique_id, ocppj_error_code_from_id(err), err_str, NULL);
			goto error;
		}
	}
	enum ocpp_charging_profile_purpose profile_purpose = charging_profile->profile_purpose;
	esp_err_t update_error = update_charging_profile(charging_profile);
	charging_profile = NULL; // responsibility to free pointer profile has been transfered by update function
	if(update_error != ESP_OK){
		ESP_LOGE(TAG, "Unable to update profile: %s", esp_err_to_name(update_error));
		reply = ocpp_create_call_error(unique_id, OCPPJ_ERROR_INTERNAL, "Error occured during update of charging profile", NULL);
		goto error;
	}else{
		reply = ocpp_create_set_charge_profile_confirmation(unique_id, OCPP_CHARGING_PROFILE_STATUS_ACCEPTED);
		if(profile_purpose == eOCPP_CHARGING_PROFILE_PURPOSE_CHARGE_POINT_MAX){
			xTaskNotify(ocpp_smart_task_handle, eNEW_PROFILE_MAX, eSetBits);
		}else{
			xTaskNotify(ocpp_smart_task_handle, eNEW_PROFILE_TX, eSetBits);
		}

		if(reply == NULL){
			ESP_LOGE(TAG, "Unable to create set charge profile confirmation");
		}else{
			send_call_reply(reply);
			cJSON_Delete(reply);
		}
	}

	return;

error:
	if(reply == NULL){
		ESP_LOGE(TAG, "No reply created");
	}else{
		send_call_reply(reply);
		cJSON_Delete(reply);
	}

	ocpp_free_charging_profile(charging_profile);
}

esp_err_t mount_partition(){
	if(mounted){
		ESP_LOGE(TAG, "Request to mount while mounted");
		return ESP_ERR_INVALID_STATE;
	}

	ESP_LOGI(TAG, "Mounting %s", base_path);
	const esp_vfs_fat_mount_config_t mount_config = {
		.max_files = (conf_max_stack_level +1) * 2,
		.format_if_mount_failed = true,
		.allocation_unit_size = CONFIG_WL_SECTOR_SIZE
	};

	esp_err_t err = esp_vfs_fat_spiflash_mount(base_path, "files", &mount_config, &s_wl_handle);
	if(err != ESP_OK){
		ESP_LOGE(TAG, "Failed to mount: %s", esp_err_to_name(err));
	}else{
		mounted = true;
	}

	return err;
}

int * active_transaction_id = NULL;

void ocpp_set_transaction_is_active(bool active){
	if(active){
		xTaskNotify(ocpp_smart_task_handle, eTRANSACTION_START, eSetBits);
	}else{
		xTaskNotify(ocpp_smart_task_handle, eTRANSACTION_STOP, eSetBits);
	}
}

void ocpp_set_active_transaction_id(int * transaction_id){
	active_transaction_id = transaction_id;

	xTaskNotify(ocpp_smart_task_handle, eTRANSACTION_ID_CHANGED, eSetBits);
}


void clear_active_transaction_id(){
	active_transaction_id = NULL;

	xTaskNotify(ocpp_smart_task_handle, eTRANSACTION_ID_CHANGED, eSetBits);
}

static time_t get_absolute_start_time(time_t relative_start, const struct ocpp_charging_profile * profile){

	//used to calculate time if recurring
	time_t tmp_time;

	switch(profile->profile_kind){
	case eOCPP_CHARGING_PROFILE_KIND_ABSOLUTE:
		return *profile->charging_schedule.start_schedule;

	case eOCPP_CHARGING_PROFILE_KIND_RECURRING:
		// Time since first start
		tmp_time = relative_start - *profile->charging_schedule.start_schedule;

		switch(*profile->recurrency_kind){
		case eOCPP_RECURRENCY_KIND_DAILY:
			tmp_time %= 86400; // 86400 = 1 day; Result should be seconds since last reccurence
			return relative_start - tmp_time; // last recurrence

		case eOCPP_RECURRENCY_KIND_WEEKLY:
			tmp_time %= 604800; // 604800 = 1 week; Result should be seconds since last reccurence
			return relative_start - tmp_time; // last recurrence
		}
		break;

	case eOCPP_CHARGING_PROFILE_KIND_RELATIVE:
		return relative_start;
	}

	ESP_LOGE(TAG, "Requested absolute start time from invalid profile");
	return relative_start;
}

// Assumes that all profiles are relevant for expected connector id, i.e expect charger to only have 1 connector and profiles to be valid for this charger.
static void get_active_profiles(time_t when, time_t relative_start, int * transaction_id, struct ocpp_charging_profile ** profile_out, struct ocpp_charging_profile * (*next_profile)(struct ocpp_charging_profile *), time_t * renewal_time_out){

	*profile_out = next_profile(NULL);
	*renewal_time_out = LONG_MAX;

	while((*profile_out)->stack_level != -1){ // stack_level -1 is used to indicate default profile if no other exists or active

		/*
		 * NOTE: A "Valid" profile here is as specified in 3.13.2:
		 * "the prevailing charging profile SHALL be the charging profile with the highest stackLevel among the profiles that
		 * are valid at that point in time, as determined by their validFrom and validTo parameters"
		 *
		 * As hinted in a note in the same section, a "prevailing charging profile" may not be the one that ends up being used:
		 * "If you use Stacking without a duration, on the highest stack level, the Charge Point will never fall back
		 * to a lower stack level profile"
		 *
		 * "Fall back" explanation is expanded in a note in section 5.16.4:
		 * "When recurrencyKind is used in combination with a chargingSchedule duration shorter than the recurrencyKind period,
		 * the Charge Point SHALL fall back to default behaviour after the chargingSchedule duration ends. This fall back means
		 * that the Charge Point SHALL use a ChargingProfile with a lower stackLevel if available."
		 */

		// Find "the prevailing charging profile"
		if((*profile_out)->valid_from < when && (*profile_out)->valid_to > when){

			// "the transactionId MAY be used to match the profile to a specific transaction."
			if((*profile_out)->transaction_id == NULL || *transaction_id == *(*profile_out)->transaction_id){

				//Fall through if "when" is not within duration
				time_t last_start = get_absolute_start_time(relative_start, *profile_out);
				if((*profile_out)->charging_schedule.duration != NULL
					&& (when - last_start > *(*profile_out)->charging_schedule.duration)){

					//If the Fall through would not happen later, we update time when profile should be renewed
					if((*profile_out)->profile_kind == eOCPP_CHARGING_PROFILE_KIND_RECURRING){
						time_t next_start;
						if(*(*profile_out)->recurrency_kind == eOCPP_RECURRENCY_KIND_DAILY){
							next_start = last_start + 86400;
						}else{
							next_start = last_start + 604800;
						}
						if(*renewal_time_out > next_start)
							*renewal_time_out = next_start;
					}

					ocpp_free_charging_profile(*profile_out);
					*profile_out = next_profile(*profile_out);
					continue;
				}
			}

			break; // No Fall through; profile is active.
		}else{
			// If it is not valid now, we check  if it may be valid later or if it can be removed
			if((*profile_out)->valid_to < time(NULL)){
				remove_profile(*profile_out, false); // Remove old profiles that can never be valid

				ocpp_free_charging_profile(*profile_out);
				*profile_out = next_profile(*profile_out);
				continue;

			}else{ // If it may be valid later
				// We store when it may become the "prevailing profile" i.e takes pecedence over active profile. If it is the next to become valid.
				if((*profile_out)->valid_from < *renewal_time_out){
					*renewal_time_out = (*profile_out)->valid_from;
				}
			}
		}

		ocpp_free_charging_profile(*profile_out);
		*profile_out = next_profile(*profile_out);
	}

	// When we have selected a profile, we check if it will become invalid due to valid_to or fall through due to duration before it
	// would otherwise be renewed by a higher priority profile
	if(*renewal_time_out > (*profile_out)->valid_to)
		*renewal_time_out = (*profile_out)->valid_to;

	time_t profile_start = get_absolute_start_time(relative_start, *profile_out);

	if((*profile_out)->charging_schedule.duration != NULL && *renewal_time_out > *(*profile_out)->charging_schedule.duration + profile_start)
		*renewal_time_out = *(*profile_out)->charging_schedule.duration + profile_start;
}

/*
 * When no charge profiles are defined we use same behaviour as specification requires while offline:
 * "[when offline], without having any charging profiles, then it SHALL execute a transaction as if no
 * constraints apply." 'No constraints' are assumed to mean only constrained by EV/EVSE limits.
 */
const struct ocpp_charging_schedule_period local_schedule_period_max = {
	.limit = 32.0f,
	.number_phases = 3
};
const float default_minimum = 6.0f;

bool get_period_from_schedule(struct ocpp_charging_schedule * schedule, uint time_since_start, enum ocpp_recurrency_kind * recurrency_kind, struct ocpp_charging_schedule_period * period_out, uint32_t * time_to_next_period){

	ESP_LOGI(TAG, "Getting period from schedule at %d", time_since_start);
	struct ocpp_charging_schedule_period_list * current_period = &schedule->schedule_period;
	struct ocpp_charging_schedule_period_list * last_period = NULL;

	/*
	 * We set time_since_start relative to last recurrence. We also set a next period default value to
	 * the next recurrency or UINT32_MAX to indicate that no next period exists in schedule, or next period
	 * would overflow uint32_t.
	 * This default value will be changed if period_out is not the last period in the list once calculated.
	 */
	if(recurrency_kind != NULL){
		if(*recurrency_kind == eOCPP_RECURRENCY_KIND_DAILY){
			time_since_start = time_since_start % (60 * 60 * 24);
			*time_to_next_period = (60 * 60 * 24) - time_since_start;

		}else if(*recurrency_kind == eOCPP_RECURRENCY_KIND_WEEKLY){
			time_since_start = time_since_start % (60 * 60 * 24 * 7);
			*time_to_next_period = (60 * 60 * 24 * 7) - time_since_start;
		}
	}else{
		*time_to_next_period = UINT32_MAX;
	}

	/*
	 * TODO:
	 * "[When not within duration], the Charge SHALL fall back to [...] use a ChargingProfile with a lower stackLevel if available.
	 * If no other ChargingProfile is available, theCharge Point SHALL allow charging as if no ChargingProfile is installed"
	 */
	if(schedule->duration != NULL && time_since_start > *schedule->duration){
		ESP_LOGE(TAG, "Schedule is invalid at requested offset from start");
		return false;
	}

	while(true){
		if(current_period == NULL || current_period->value.start_period > time_since_start){

			period_out->start_period = last_period->value.start_period;
			period_out->limit = last_period->value.limit;
			period_out->number_phases = last_period->value.number_phases;

			if(current_period != NULL){
				*time_to_next_period = current_period->value.start_period - time_since_start;
			}

			return true;
		}else{
			if(current_period->value.start_period == time_since_start){

				period_out->start_period = current_period->value.start_period;
				period_out->limit = current_period->value.limit;
				period_out->number_phases = current_period->value.number_phases;

				if(current_period->next != NULL){
					*time_to_next_period = current_period->next->value.start_period - time_since_start;
				}

				return true;
			}else{
				last_period = current_period;
				current_period = current_period->next;
			}
		}
	}
}

static esp_err_t extend_composite_schedule(time_t when, time_t relative_start, uint32_t wanted_duration, size_t max_periods,
					struct ocpp_charging_profile * profile_tx, struct ocpp_charging_profile * profile_max,
					struct ocpp_charging_schedule_period_list * period_list_out, int old_duration, int * new_duration_out){

	ESP_LOGI(TAG, "Extending composite schedule");

	uint8_t created_periods = 0;
	uint time_since_start_tx = when - get_absolute_start_time(relative_start, profile_tx);
	uint time_since_start_max = when - get_absolute_start_time(relative_start, profile_max);

	uint duration_created = 0;

	struct ocpp_charging_schedule_period period_tx;
	uint32_t next_period_tx = 0;

	struct ocpp_charging_schedule_period period_max;
	uint32_t next_period_max = 0;

	struct ocpp_charging_schedule_period_list * period_list = period_list_out;
	struct ocpp_charging_schedule_period_list * last_period = NULL;

	while(duration_created < wanted_duration && created_periods < max_periods){

		if(duration_created >= next_period_tx){
			uint32_t next_period_offset;
			if(!get_period_from_schedule(&profile_tx->charging_schedule, time_since_start_tx + duration_created,
							profile_tx->recurrency_kind, &period_tx, &next_period_offset)){

				ESP_LOGE(TAG, "Period invalid for tx profile, using default");
				period_tx = local_schedule_period_max;
			}

			if(next_period_offset >= wanted_duration){
				next_period_tx = wanted_duration;
			}else{
				next_period_tx += next_period_offset;
			}
		}

		if(duration_created >= next_period_max){
			uint32_t next_period_offset;
			if(!get_period_from_schedule(&profile_max->charging_schedule, time_since_start_max + duration_created,
							profile_max->recurrency_kind, &period_max, &next_period_offset)){

				ESP_LOGE(TAG, "Period invalid for max profile, using default");
				period_max = local_schedule_period_max;
			}

			if(next_period_offset >= wanted_duration){
				next_period_max = wanted_duration;
			}else{
				next_period_max += next_period_offset;
			}
		}

		if(period_list == NULL){
			period_list = malloc(sizeof(struct ocpp_charging_schedule_period_list));
			if(period_list == NULL){
				ESP_LOGE(TAG, "Unable to allocate memory for next schedule period");
				return ESP_ERR_NO_MEM;
			}
			period_list->next = NULL;
		}

		period_list->value.limit = (period_tx.limit < period_max.limit) ? period_tx.limit : period_max.limit;
		period_list->value.number_phases = (period_tx.number_phases < period_max.number_phases) ? period_tx.number_phases : period_max.number_phases;

		period_list->value.start_period = duration_created + old_duration;
		duration_created = (next_period_tx < next_period_max) ? next_period_tx : next_period_max;

		created_periods++;

		if(last_period != NULL)
			last_period->next = period_list;

		last_period = period_list;
		period_list = period_list->next;
	}
	*new_duration_out = when + duration_created;

	if(duration_created >= wanted_duration){
		return ESP_OK;
	}else if(created_periods < max_periods){
		return ESP_ERR_INVALID_SIZE;
	}else{
		return ESP_FAIL;
	}
}

static esp_err_t create_composite_schedule(time_t when, time_t relative_start,
					uint32_t wanted_duration, enum ocpp_charging_rate_unit charge_rate_unit,
					struct ocpp_charging_profile * profile_tx, struct ocpp_charging_profile * profile_max,
					struct ocpp_charging_schedule * schedule_out){

	ESP_LOGI(TAG, "Creating composite schedule");

	if(charge_rate_unit != eOCPP_CHARGING_RATE_A){
		ESP_LOGE(TAG, "Unsupported charging rate unit");

		schedule_out->schedule_period.value = local_schedule_period_max;
		return ESP_ERR_NOT_SUPPORTED;
	}
	schedule_out->charge_rate_unit = charge_rate_unit;

	if(!(profile_tx->valid_from < relative_start && profile_max->valid_from < relative_start
			&& profile_tx->valid_to > relative_start && profile_max->valid_to > relative_start)){

		ESP_LOGE(TAG, "Requested composit schedule is not valid at requested start time");

		schedule_out->schedule_period.value = local_schedule_period_max;
		return ESP_ERR_INVALID_ARG;
	}

	schedule_out->start_schedule = malloc(sizeof(time_t));
	if(schedule_out->start_schedule == NULL){
		ESP_LOGE(TAG, "Unable to allocate memory for composite schedule start time");

		schedule_out->schedule_period.value = local_schedule_period_max;
		return ESP_ERR_NO_MEM;
	}
	*schedule_out->start_schedule = when;

	schedule_out->duration = malloc(sizeof(int));
	if(schedule_out->duration == NULL){
		ESP_LOGE(TAG, "Unable to allocate space for schedule duration");
		free(schedule_out->start_schedule);

		schedule_out->schedule_period.value = local_schedule_period_max;
		return ESP_ERR_NO_MEM;
	}
	*schedule_out->duration = 0;

	schedule_out->min_charging_rate = 6.0f;
	if(schedule_out->min_charging_rate < profile_tx->charging_schedule.min_charging_rate){
		schedule_out->min_charging_rate = profile_tx->charging_schedule.min_charging_rate;
	}

	if(schedule_out->min_charging_rate < profile_max->charging_schedule.min_charging_rate){
		schedule_out->min_charging_rate = profile_max->charging_schedule.min_charging_rate;
	}

	return extend_composite_schedule(when, relative_start, wanted_duration, conf_max_periods,
					profile_tx, profile_max, &schedule_out->schedule_period, 0, schedule_out->duration);
}

void get_composite_schedule_cb(const char * unique_id, const char * action, cJSON * payload, void * cb_data){
	ESP_LOGI(TAG, "Received request for get composite schedule");

	char err_str[124] = {0};

	int connector_id;
	enum ocppj_err_t err = ocppj_get_int_field(payload, "connectorId", true, &connector_id, err_str, sizeof(err_str));
	if(err != eOCPPJ_NO_ERROR){
		ESP_LOGW(TAG, "Invalid connectorId in request: '%s'", err_str);
		goto error;
	}

	int duration;
	err = ocppj_get_int_field(payload, "duration", true, &duration, err_str, sizeof(err_str));
	if(err != eOCPPJ_NO_ERROR){
		ESP_LOGW(TAG, "Invalid duration in request: '%s'", err_str);
		goto error;
	}

	char * charging_rate_unit = NULL;
	enum ocpp_charging_rate_unit unit_id = eOCPP_CHARGING_RATE_A;

	err = ocppj_get_string_field(payload, "chargingRateUnit", false, &charging_rate_unit, err_str, sizeof(err_str));
	if(err != eOCPPJ_NO_VALUE){

		if(err == eOCPPJ_NO_ERROR){
			ESP_LOGW(TAG, "Invalid chargingRateUnit in request: '%s'", err_str);
			goto error;
		}

		if(ocpp_validate_enum(charging_rate_unit, true, 1,
					OCPP_CHARGING_RATE_A,
					OCPP_CHARGING_RATE_W) != 0){

			err = eOCPPJ_ERROR_TYPE_CONSTRAINT_VIOLATION;
			strcpy(err_str, "Expected 'chargingRateUnit' to be ChargingRateUnit type");
			goto error;

		}

		if(!ocpp_csl_contains(conf_allowed_charging_rate_unit, charging_rate_unit)){
			err = eOCPPJ_ERROR_NOT_SUPPORTED;
			strcpy(err_str, "'Requested chargingRateUnit' is not supported");
			goto error;
		}

		unit_id = ocpp_charging_rate_unit_to_id(charging_rate_unit);
	}

	struct ocpp_charging_profile * profile_tx = NULL; // tx or txDefault profile
	struct ocpp_charging_profile * profile_max = NULL;
	struct ocpp_charging_schedule * schedule = NULL;

	time_t start_time = time(NULL);

	time_t renewal_time_tx = 0;
	time_t renewal_time_max = 0;

	int duration_created = 0;
	int segment_duration_created;

	while(duration > duration_created){
		segment_duration_created = 0;
		time_t creation_time = start_time + duration_created;

		if(creation_time >= renewal_time_tx){
			ocpp_free_charging_profile(profile_tx);

			get_active_profiles(creation_time, start_time, active_transaction_id,
					&profile_tx, next_tx_or_tx_default_profile, &renewal_time_tx);
		}

		if(creation_time >= renewal_time_max){
			ocpp_free_charging_profile(profile_max);

			get_active_profiles(creation_time, start_time, active_transaction_id,
					&profile_max, next_max_profile, &renewal_time_max);
		}

		uint segment_duration;
		if(renewal_time_max < renewal_time_tx){
			segment_duration = renewal_time_max - creation_time;
		}else{
			segment_duration = renewal_time_tx - creation_time;
		}

		if(segment_duration < 1){
			ESP_LOGE(TAG, "Expected segment duration is insufficient");
			segment_duration = 1;
		}

		struct ocpp_charging_schedule_period_list * period_list = &schedule->schedule_period;
		size_t period_count = 0;

		esp_err_t schedule_error = ESP_OK;
		if(schedule == NULL){
			schedule_error = create_composite_schedule(creation_time, start_time, segment_duration, unit_id,
							profile_tx, profile_max, schedule);
			if(schedule->duration != NULL){
				segment_duration_created = *schedule->duration;
			}else{
				ESP_LOGE(TAG, "Unable to determin duration of composite schedule");
				goto error;
			}
		}else{
			schedule_error = extend_composite_schedule(creation_time, start_time, segment_duration, conf_max_periods - period_count,
								profile_tx, profile_max, period_list->next, duration_created, &segment_duration_created);

			*schedule->duration += segment_duration_created;
		}

		if(schedule_error != ESP_OK){
			if(err == ESP_ERR_INVALID_SIZE){
				ESP_LOGW(TAG, "Unable to fill requested duration; would exceed max periods");
				break;
			}else{
				ESP_LOGE(TAG, "Unable to create composite schedule: %s", esp_err_to_name(err));
				err = eOCPPJ_ERROR_INTERNAL;
				sprintf(err_str, "Error occured while attempting to create composite schedule");
				goto error;
			}
		}

		while(period_list != NULL){
			period_count++;
			period_list = period_list->next;
		}

		if(segment_duration_created == 0){
			ESP_LOGE(TAG, "Unable to continue schdule creation");
			break;
		}else{
			duration_created += segment_duration_created;
		}
	}

	cJSON * reply = ocpp_create_get_composite_schedule_confirmation(unique_id, OCPP_GET_COMPOSITE_SCHEDULE_STATUS_ACCEPTED,
									&connector_id, &start_time, schedule);
	ocpp_free_charging_schedule(schedule);

	if(reply != NULL){
		send_call_reply(reply);
		cJSON_Delete(reply);
	}else{
		ESP_LOGE(TAG, "Unable to create ocpp error for not implemented");
		err = eOCPPJ_ERROR_INTERNAL;
		sprintf(err_str, "Error occured while attempting to create GetCompositeSchedule.conf");
		goto error;
	}

error:
	if(err == eOCPPJ_NO_ERROR){
		ESP_LOGE(TAG, "Unknown error occured during get composite schedule");
		err = eOCPPJ_ERROR_INTERNAL;
	}

	cJSON * error_reply = ocpp_create_call_error(unique_id, ocppj_error_code_from_id(err), err_str, NULL);
	if(error_reply == NULL){
		ESP_LOGE(TAG, "Unable to create error reply");
	}else{
		send_call_reply(error_reply);
	}
}


static void ocpp_smart_task(){
	time_t current_time = time(NULL);

	// Will be created when a transaction starts
	struct ocpp_charging_profile * profile_tx = NULL; // tx or txDefault profile
	struct ocpp_charging_profile * profile_max = NULL;
	struct ocpp_charging_schedule * schedule = NULL;
	struct ocpp_charging_schedule_period current_period = {
		.start_period = -1,
		.limit = -1,
		.number_phases = -1,
	};

	float current_min = -1.0f;

	// When data related to charging variables may change.
	time_t renewal_time_tx = LONG_MAX;
	time_t renewal_time_max = LONG_MAX;
	time_t renewal_time_schedule = LONG_MAX;
	time_t renewal_time_period = LONG_MAX;

	// Time until next predicted change event
	uint32_t next_renewal_delay = UINT32_MAX;

	bool transaction_is_active = false;
	time_t transaction_start_time;

	while(true){

		uint32_t data = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(next_renewal_delay * 1000));
		current_time = time(NULL);

		float last_min = current_min;
		struct ocpp_charging_schedule_period last_period = current_period;

		bool last_is_active = transaction_is_active;

		ESP_LOGI(TAG, "Handling event mask %x, Renewal delay was: %d", data, next_renewal_delay);

		if(data & eNOT_NEEDED){
			ESP_LOGI(TAG, "Smart charging stopped");
			break;
		}

		if(data & eTRANSACTION_START){

			if(data & eTRANSACTION_STOP){
				ESP_LOGE(TAG, "Transaction started and stopped since last update, unable to confirm if active. Checking transaction id");
				if(active_transaction_id != NULL){
					ESP_LOGW(TAG, "Transaction_id set, expecting active transaction");
					transaction_is_active = true;
					transaction_start_time = current_time;
				}else{
					ESP_LOGW(TAG, "Transaction_id not set, expecting no transaction");
					transaction_is_active = false; // Could be wrong in offline or if delay between start and setting id
					remove_profile_from_memory(NULL, NULL, true);
				}
			}else{
				transaction_is_active = true;
				transaction_start_time = current_time;
			}

		}else if(data & eTRANSACTION_STOP){
			transaction_is_active = false;
			remove_profile_from_memory(NULL, NULL, true);
		}

		if(data & eTRANSACTION_ID_CHANGED){
			if(!transaction_is_active && active_transaction_id != NULL){
				ESP_LOGW(TAG, "Transaction id set, but no start transaction recieved, expecting active transaction");
				transaction_is_active = true;
				transaction_start_time = current_time;
			}
		}

		if(!last_is_active && transaction_is_active){
			ESP_LOGI(TAG, "Transaction changed from inactive to active");
			data |= eACTIVE_PROFILE_TX_CHANGE;
			data |= eACTIVE_PROFILE_MAX_CHANGE;
		}

		if(transaction_is_active){
			ESP_LOGI(TAG, "Handling active transaction");
			if(profile_tx == NULL || renewal_time_tx < current_time){
				ESP_LOGI(TAG, "Current tx profile timed out");
				data |= eACTIVE_PROFILE_TX_CHANGE;
			}

			if(profile_max == NULL || renewal_time_max < current_time){
				ESP_LOGI(TAG, "Current max profile timed out");
				data |= eACTIVE_PROFILE_MAX_CHANGE;
			}

			if(schedule == NULL || renewal_time_schedule < current_time){
				ESP_LOGI(TAG, "Current schedule timed out");
				data |= eSCHEDULE_CHANGE;
			}

			if(renewal_time_period < current_time){
				ESP_LOGI(TAG, "Current period timed out");
				data |= eACTIVE_PERIOD_CHANGE;
			}

			if(data & eTRANSACTION_ID_CHANGED){
				ESP_LOGI(TAG, "Transaction id changed");
				data |= eACTIVE_PROFILE_TX_CHANGE;
			}

			if(data & eNEW_PROFILE_TX || data & eACTIVE_PROFILE_TX_CHANGE){
				ESP_LOGI(TAG, "Updating tx profile");

				int old_id = (profile_tx != NULL) ? profile_tx->stack_level : INT_MIN;

				if(profile_tx != NULL && profile_tx->profile_purpose != eOCPP_CHARGING_PROFILE_PURPOSE_TX) //tx profiles are deleted after transaction end
					ocpp_free_charging_profile(profile_tx);

				profile_tx = NULL;

				get_active_profiles(current_time, transaction_start_time, active_transaction_id,
						&profile_tx, next_tx_or_tx_default_profile, &renewal_time_tx);

				//Update schedule if needed
				if(data & eNEW_PROFILE_TX // Needed if given new profile, as curremt profile may heve been overwritten with same profile_id
					|| (old_id != profile_tx->profile_id)){ // Needed if different profile

					ESP_LOGI(TAG, "Profile did change; requesting new schedule update");
					data |= eSCHEDULE_CHANGE;
				}else{
					ESP_LOGI(TAG, "Profile did not change");
				}
			}

			if(data & eNEW_PROFILE_MAX || data & eACTIVE_PROFILE_MAX_CHANGE){
				ESP_LOGI(TAG, "Updating max profile");

				int old_id = (profile_tx != NULL) ? profile_tx->stack_level : INT_MIN;

				ocpp_free_charging_profile(profile_max);
				profile_max = NULL;

				get_active_profiles(current_time, transaction_start_time, active_transaction_id,
						&profile_max, next_max_profile, &renewal_time_max);

				if(data & eNEW_PROFILE_MAX
					|| (old_id != profile_tx->profile_id)){

					ESP_LOGI(TAG, "Profile did change; requesting schedule updating");
					data |= eSCHEDULE_CHANGE;
				}else{
					ESP_LOGI(TAG, "Profile did not change");
				}

			}

			if(data & eSCHEDULE_CHANGE){
				ESP_LOGI(TAG, "Updating composite schedule");

				// Re-allocate the structure to free all optional values. TODO: Change to only re-allocate when needed
				ocpp_free_charging_schedule(schedule);
				schedule = calloc(sizeof(struct ocpp_charging_schedule), 1);
				if(schedule == NULL){
					ESP_LOGE(TAG, "Unable to allocate memory for composite schedule");
					goto error;
				}

				time_t tmp_schedule_dt; // length of schedule to create
				if(renewal_time_tx < renewal_time_max){
					tmp_schedule_dt = renewal_time_tx - current_time;
				}else{
				        tmp_schedule_dt = renewal_time_max - current_time;
				}

				if(tmp_schedule_dt < 0){
					tmp_schedule_dt = 1; // Create schedule for at least 1 second
				}

				create_composite_schedule(current_time, transaction_start_time,
							(tmp_schedule_dt < UINT32_MAX) ? tmp_schedule_dt: UINT32_MAX, eOCPP_CHARGING_RATE_A,
							profile_tx, profile_max, schedule);

				current_min = schedule->min_charging_rate;
				if(schedule->duration == NULL){
					renewal_time_schedule = LONG_MAX;
				}else{
					renewal_time_schedule = current_time + *schedule->duration;
				}
				data |= eACTIVE_PERIOD_CHANGE;
			}

			if(data & eACTIVE_PERIOD_CHANGE){
				ESP_LOGI(TAG, "Updating period");
				uint32_t renewal_delay_period;
				if(!get_period_from_schedule(schedule, current_time - *schedule->start_schedule, NULL,
				 				&current_period, &renewal_delay_period)){

					ESP_LOGE(TAG, "Failed to get period from composite schedule; setting default");

					current_period = local_schedule_period_max;
					current_min = default_minimum;
					renewal_delay_period = UINT32_MAX;
				}

				if(renewal_delay_period <= 0 || LONG_MAX - current_time < renewal_delay_period){
					renewal_time_period = LONG_MAX;
				}else{
					renewal_time_period = current_time + renewal_delay_period;
				}
			}

			if(last_min != current_min
				|| current_period.limit != last_period.limit
				|| current_period.number_phases != last_period.number_phases){

				ESP_LOGI(TAG, "Charging variables changed: min: %f, limit: %f, phases: %d",
					current_min, current_period.limit, current_period.number_phases);
				if(charge_value_cb != NULL){
					charge_value_cb(current_min, current_period.limit, current_period.number_phases);
				}else{
					ESP_LOGE(TAG, "Updated charging variables but no callback set");
				}

				last_min = current_min;
				last_period = current_period;
			}

			time_t next_renewal_timestamp = (renewal_time_tx < renewal_time_max) ? renewal_time_tx : renewal_time_max;

			if(next_renewal_timestamp > renewal_time_period){
				next_renewal_timestamp = renewal_time_period;
			}

			if(next_renewal_timestamp - current_time < 1){
				ESP_LOGW(TAG, "Unexpectedly low charging variable renewal delay: %ld", next_renewal_timestamp - current_time);
				next_renewal_delay = 1;
			}else{
				next_renewal_delay = next_renewal_timestamp - current_time;
			}
		}else{ // if(!transaction_is_active)
			ESP_LOGI(TAG, "Transaction is not active");
			next_renewal_delay = UINT32_MAX; // No predictable events until notified by sessionHandler.
		}
	}

error:
	ESP_LOGE(TAG, "Smart charging exited with error");
	ocpp_free_charging_profile(profile_tx); // tx or txDefault profile
	ocpp_free_charging_profile(profile_max);
	ocpp_free_charging_schedule(schedule);
}

esp_err_t ocpp_smart_charging_init(size_t connector_count, int max_stack_level,
				const char * allowed_charging_rate_unit, int max_periods, int max_charging_profiles){
	ESP_LOGI(TAG, "Initializing smart charging");

	if(strcmp(allowed_charging_rate_unit, "A") != 0){
		ESP_LOGE(TAG, "Currently only supports charge rate unit A");
		return ESP_ERR_NOT_SUPPORTED;
	}

	conf_connector_count = connector_count;
	conf_max_stack_level = max_stack_level;
	strcpy(conf_allowed_charging_rate_unit, allowed_charging_rate_unit);
	conf_max_periods = max_periods;
	conf_max_charging_profiles = max_charging_profiles;

	file_lock = xSemaphoreCreateMutex();
	if(file_lock == NULL){
		ESP_LOGE(TAG, "Unable to create mutex");
		return ESP_ERR_NO_MEM;
	}

	esp_err_t err = mount_partition();
	if(err != ESP_OK){
		xSemaphoreGive(file_lock);
		return err;
	}

	tx_profiles = calloc(sizeof(struct ocpp_charging_profile *), conf_max_stack_level+1);
	if(tx_profiles == NULL){
		ESP_LOGE(TAG, "Unable to allocate space for tx profiles");

		vSemaphoreDelete(file_lock);
		file_lock = NULL;

		return ESP_ERR_NO_MEM;
	}

	attach_call_cb(eOCPP_ACTION_CLEAR_CHARGING_PROFILE_ID, clear_charging_profile_cb, NULL);
	attach_call_cb(eOCPP_ACTION_GET_COMPOSITE_SCHEDULE_ID, get_composite_schedule_cb, NULL);
	attach_call_cb(eOCPP_ACTION_SET_CHARGING_PROFILE_ID, set_charging_profile_cb, NULL);

	xTaskCreate(ocpp_smart_task, "ocpp_smart_task", 4096, NULL, 2, &ocpp_smart_task_handle);

	xSemaphoreGive(file_lock);

	return ESP_OK;
}

void ocpp_set_on_new_period_cb(void (* on_new_period)(float min_charging_limit, float max_charging_limit, uint8_t number_phases)){
	charge_value_cb = on_new_period;
}
