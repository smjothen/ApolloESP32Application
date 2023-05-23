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
static const char * base_path = CONFIG_OCPP_FILE_PATH;

static SemaphoreHandle_t file_lock = NULL;

static TaskHandle_t ocpp_smart_task_handle = NULL;

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

esp_err_t update_charging_profile(struct ocpp_charging_profile * profile){
	ESP_LOGI(TAG, "Updating charging profile");

	struct stat st;
	if(stat(base_path, &st) != 0){
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
		xTaskNotify(ocpp_smart_task_handle, eNEW_PROFILE_TX, eSetBits);

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

	if(optional_period_count > 0){

		if(optional_period_count > CONFIG_OCPP_CHARGING_SCHEDULE_MAX_PERIODS){
			ESP_LOGE(TAG, "Period count exceeds max count when writing to file");
			goto error;
		}

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

	if(profile->profile_purpose == eOCPP_CHARGING_PROFILE_PURPOSE_CHARGE_POINT_MAX){
		xTaskNotify(ocpp_smart_task_handle, eNEW_PROFILE_MAX, eSetBits);
	}else{
		xTaskNotify(ocpp_smart_task_handle, eNEW_PROFILE_TX, eSetBits);
	}

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
		for(size_t i = 0; i < CONFIG_OCPP_CHARGE_PROFILE_MAX_STACK_LEVEL+1; i++){
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
		for(size_t i = 0; i < CONFIG_OCPP_CHARGE_PROFILE_MAX_STACK_LEVEL+1; i++){
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
		ESP_LOGE(TAG, "Unable to allocate memory for base64 string of profile (length %d)", out_length);
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
			goto error;
		}

		if(periods_count == 0 || periods_count > CONFIG_OCPP_CHARGING_SCHEDULE_MAX_PERIODS){
			ESP_LOGE(TAG, "Read charging profile has invalid period count");
			goto error;
		}

		size_t out_length;
		if(fread(&out_length, sizeof(size_t), 1, fp) != 1){
			ESP_LOGE(TAG, "Unable to read lenght of the base64 periods string");
			goto error;
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

		if(decoded_length != sizeof(struct ocpp_charging_schedule_period) * periods_count){
			ESP_LOGE(TAG, "Unexpected length of decoded data");
			free(periods);
			goto error;
		}

		failed_from = eFROM_PERIOD;

		struct ocpp_charging_schedule_period_list * entry = &profile->charging_schedule.schedule_period;

		for(size_t i = 0; i < periods_count; i++){
			entry->next = malloc(sizeof(struct ocpp_charging_schedule_period_list));
			entry = entry->next;

			if(entry == NULL){
				ESP_LOGE(TAG, "Unable to allcate memory for new period entry");
				goto error;
			}else{
				entry->next = NULL;
			}

			memcpy(&entry->value, &periods[i], sizeof(struct ocpp_charging_schedule_period));
		}
	}

	fclose(fp);
	xSemaphoreGive(file_lock);

	ESP_LOGI(TAG, "Charge profile read: %s", profile_path);
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
	errno = 0;
	if(remove(profile_path) != 0){
		ESP_LOGE(TAG, "Unable to remove '%s': %s", profile_path, strerror(errno));
	}
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

	struct ocpp_charging_profile * result = NULL;
	int requested_stack_level = (current_profile != NULL) ? current_profile->stack_level-1 : CONFIG_OCPP_CHARGE_PROFILE_MAX_STACK_LEVEL;

	for(; requested_stack_level >= 0; requested_stack_level--){
		if(tx_profiles[requested_stack_level] != NULL){
			result = tx_profiles[requested_stack_level];
			break;
		}
	}

	xSemaphoreGive(file_lock);
	return result;
}

struct ocpp_charging_profile * next_charge_profile_from_file(struct ocpp_charging_profile * current_profile,
							enum ocpp_charging_profile_purpose purpose){

	ESP_LOGI(TAG, "Request for next profile.");

	if(purpose != eOCPP_CHARGING_PROFILE_PURPOSE_CHARGE_POINT_MAX && purpose != eOCPP_CHARGING_PROFILE_PURPOSE_TX_DEFAULT){
		ESP_LOGE(TAG, "Only tx default and max profiles are stored on file");
		return NULL;
	}

	struct stat st;
	if(stat(base_path, &st) != 0){
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

	struct ocpp_charging_profile * result = read_profile_from_file(profile_path);

	xSemaphoreGive(file_lock);
	return result;
}

struct ocpp_charging_profile * next_tx_or_tx_default_profile(struct ocpp_charging_profile * current_profile){
	struct ocpp_charging_profile * result = NULL;

	if(current_profile == NULL || current_profile->profile_purpose == eOCPP_CHARGING_PROFILE_PURPOSE_TX){
		result = ocpp_duplicate_charging_profile(next_tx_profile(current_profile));
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
	}else{
		ESP_LOGE(TAG, "Unable to create ClearChargingProfile.conf");
	}

	return;

error:
	if(err != eOCPPJ_NO_ERROR && eOCPPJ_NO_VALUE){
		cJSON * reply = ocpp_create_call_error(unique_id, ocppj_error_code_from_id(err), err_str, NULL);
		if(reply != NULL){
			send_call_reply(reply);
		}
	}else{
		ESP_LOGE(TAG, "Error occured during clear charging profile, but no ocpp json error set");
	}
}

bool transaction_is_active = false;

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

			if(connector_id < 0 || connector_id > CONFIG_OCPP_NUMBER_OF_CONNECTORS){
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
		enum ocppj_err_t err =  ocpp_charging_profile_from_json(charging_profile_json, CONFIG_OCPP_CHARGE_PROFILE_MAX_STACK_LEVEL,
									ocpp_get_allowed_charging_rate_units(),
									CONFIG_OCPP_CHARGING_SCHEDULE_MAX_PERIODS,
									charging_profile, err_str, sizeof(err_str));

		if(err != eOCPPJ_NO_ERROR){
			ESP_LOGW(TAG, "Invalid charging profile: %s", err_str);

			reply = ocpp_create_call_error(unique_id, ocppj_error_code_from_id(err), err_str, NULL);
			goto error;
		}
	}

	if(charging_profile->profile_purpose == eOCPP_CHARGING_PROFILE_PURPOSE_TX && transaction_is_active == false){
		ESP_LOGW(TAG, "Attempt to set TxProfile without an active transaction");

		reply = ocpp_create_call_error(unique_id, OCPPJ_ERROR_GENERIC, "TxProfile can only be set when an a transaction is active or in a RemoteStartTransaction.req", NULL);
		goto error;
	}

	esp_err_t update_error = update_charging_profile(charging_profile);
	charging_profile = NULL; // responsibility to free pointer profile has been transfered by update function
	if(update_error != ESP_OK){
		ESP_LOGE(TAG, "Unable to update profile: %s", esp_err_to_name(update_error));
		reply = ocpp_create_call_error(unique_id, OCPPJ_ERROR_INTERNAL, "Error occured during update of charging profile", NULL);
		goto error;
	}

	reply = ocpp_create_set_charge_profile_confirmation(unique_id, OCPP_CHARGING_PROFILE_STATUS_ACCEPTED);
	if(reply == NULL){
		ESP_LOGE(TAG, "Unable to create set charge profile confirmation");
	}else{
		send_call_reply(reply);
	}

	return;

error:
	if(reply == NULL){
		ESP_LOGE(TAG, "No reply created");
	}else{
		send_call_reply(reply);
	}

	ocpp_free_charging_profile(charging_profile);
}

int * active_transaction_id = NULL;
time_t transaction_start_time = 0;

void ocpp_set_transaction_is_active(bool active, time_t start_time){

	transaction_start_time = start_time;
	if(ocpp_smart_task_handle == NULL)
		return;

	if(active){
		xTaskNotify(ocpp_smart_task_handle, eTRANSACTION_START, eSetBits);
	}else{
		xTaskNotify(ocpp_smart_task_handle, eTRANSACTION_STOP, eSetBits);
	}
}

void ocpp_set_active_transaction_id(int * transaction_id){
	active_transaction_id = transaction_id;

	if(ocpp_smart_task_handle != NULL)
		xTaskNotify(ocpp_smart_task_handle, eTRANSACTION_ID_CHANGED, eSetBits);
}

/**
 * @brief Gets absolute start time of a schedule and time since it last started.
 *
 * @param relative_start Time when charging started or GetCompositeSchedule.req was received.
 * @param sec_since_start Time since relative_start
 * @param profile Profile containing relevant schedule
 * @param absolute_start_out Output parameter for when the schedule last started.
 * @param schedule_offset_out Output parameter for current time since last start
 *
 * @return ESP_OK on success. ESP_ERR_INVALID_ARG if invalid profile is detected. ESP_ERR_INVALID_STATE if requested time is outside valid_from -> valid_to range or start_schedule.
 */
esp_err_t get_normalized_time(time_t relative_start, int time_since_start, const struct ocpp_charging_profile * profile, time_t * absolute_start_out, int * schedule_offset_out){

	if(profile == NULL){
		ESP_LOGE(TAG, "No profile provided");
		return ESP_ERR_INVALID_ARG;
	}

	time_t when = relative_start + time_since_start;

	if(when < profile->valid_from
		|| when > profile->valid_to)
		return ESP_ERR_INVALID_STATE;

	uint recurrency_interval;

	switch(profile->profile_kind){
	case eOCPP_CHARGING_PROFILE_KIND_ABSOLUTE:

		if(profile->charging_schedule.start_schedule == NULL)
			return ESP_ERR_INVALID_ARG;

		if(when < *profile->charging_schedule.start_schedule)
			return ESP_ERR_INVALID_STATE;

		if(absolute_start_out != NULL)
			*absolute_start_out = *profile->charging_schedule.start_schedule;

		if(schedule_offset_out != NULL)
			*schedule_offset_out = when - *profile->charging_schedule.start_schedule;

		break;

	case eOCPP_CHARGING_PROFILE_KIND_RECURRING:

		if(profile->charging_schedule.start_schedule == NULL || profile->recurrency_kind == NULL)
			return ESP_ERR_INVALID_ARG;

		if(when < *profile->charging_schedule.start_schedule)
			return ESP_ERR_INVALID_STATE;

		switch(*profile->recurrency_kind){
		case eOCPP_RECURRENCY_KIND_DAILY:
			recurrency_interval = 86400; // 86400 = 1 day in second
			break;

		case eOCPP_RECURRENCY_KIND_WEEKLY:
			recurrency_interval = 604800; // 604800 = 1 week in seconds
			break;

		default:
			ESP_LOGE(TAG, "Invalid recurrency kind");
			return ESP_ERR_INVALID_ARG;
		}

		if(absolute_start_out != NULL)
			*absolute_start_out =  when - ((when - *profile->charging_schedule.start_schedule) % recurrency_interval);

		if(schedule_offset_out != NULL)
			*schedule_offset_out = (when - *profile->charging_schedule.start_schedule) % recurrency_interval;
		break;

	case eOCPP_CHARGING_PROFILE_KIND_RELATIVE:

		if(absolute_start_out != NULL)
			*absolute_start_out = relative_start;

		if(schedule_offset_out != NULL)
			*schedule_offset_out = time_since_start;
		break;
	}

	return ESP_OK;
}

/**
 * @brief returns the time when it will become valid starting at relative_start + relative_offset.
 */
static time_t get_when_schedule_is_active(time_t relative_start, int relative_offset, struct ocpp_charging_profile * profile, int * transaction_id){
	if(profile->transaction_id != NULL && (transaction_id == NULL || transaction_id != profile->transaction_id)) // Invalid due to transaction id mismatch
		return LONG_MAX;

	// Find the earliest time it can be active from

	time_t active_from = profile->valid_from;

	if(profile->charging_schedule.start_schedule != NULL && *profile->charging_schedule.start_schedule > active_from)
		active_from = *profile->charging_schedule.start_schedule;

	// Earliest relevant time is after the transaction start or composite shcedule was requested
	if(relative_start + relative_offset > active_from)
		active_from = relative_start + relative_offset;

	int absolute_offset;
	time_t absolute_start;
	if(get_normalized_time(relative_start, relative_offset, profile, &absolute_start, &absolute_offset) != ESP_OK)
		return LONG_MAX;

	if(profile->charging_schedule.duration != NULL && absolute_offset >= *profile->charging_schedule.duration){
		if(profile->profile_kind != eOCPP_CHARGING_PROFILE_KIND_RECURRING || profile->recurrency_kind == NULL)
			return LONG_MAX; // Will not become valid since it is not recurring schedule

		switch(*profile->recurrency_kind){
		case eOCPP_RECURRENCY_KIND_DAILY:
			active_from = absolute_start + 86400;
			break;

		case eOCPP_RECURRENCY_KIND_WEEKLY:
			active_from = absolute_start + 604800;
			break;

		default: // invalid
			return LONG_MAX;
		}
	}

	// Check if it is still valid
	return (profile->valid_to > active_from) ? active_from : LONG_MAX;
}

/**
 * @brief copy the period list from a profile at an offset into a new list with a relative offset. Its the callers resposibility to free the returned value.
 *
 * @param start time Relative start time.
 * @param offset The initial offset from relative start in the profils period list from witch to start copying.
 * @param end The last time at witch a period may be copied from.
 * @param profile Profile to copy from.
 * @param max_copies Maximum number of period list items in output. Must be a value greater than 0.
 * @param period_out Period list to extend. Will overwrite its value amd potentially its next pointer.
 * @param offset_end_out Output parameter to indicate when output list may not be valid.
 * @param copy_count_out Output parameter with number of elemets copied.
 *
 * @return pointer to copied period list.
 */
struct ocpp_charging_schedule_period_list * copy_period_at(time_t start, int offset, time_t end, struct ocpp_charging_profile * profile,
							size_t max_copies, int * offset_end_out, int * copy_count_out){

	struct ocpp_charging_schedule_period_list * period = &profile->charging_schedule.schedule_period;
	struct ocpp_charging_schedule_period_list * period_last = NULL;

	time_t abs_start;
	int abs_offset;

	if(get_normalized_time(start, offset, profile, &abs_start, &abs_offset) != ESP_OK){
		ESP_LOGE(TAG, "Unable to get normalized time for copying periods");
		return NULL;
	}

	struct ocpp_charging_schedule_period_list * copy = calloc(sizeof(struct ocpp_charging_schedule_period_list), 1);
	if(copy == NULL){
		ESP_LOGE(TAG, "Unable to create buffer for copying period list");
		return NULL;
	}

	struct ocpp_charging_schedule_period_list * ret = copy; // save the head of the copy for return.

	int offset_diff = offset - abs_offset;
	while(period != NULL && period->value.start_period < abs_offset){ // Move period pointer to first within requested range
		period_last = period;
		period = period->next;
	}

	if(period != NULL && period->value.start_period == abs_offset){
		ESP_LOGI(TAG, "Offset on start_period");

		copy->value.start_period = offset;
		copy->value.limit = period->value.limit;
		copy->value.number_phases = period->value.number_phases;

		period_last = period;
		period = period->next;

	} else { // requested offset is part of previous list entry
		ESP_LOGI(TAG, "Offset on start_period");

		copy->value.start_period = offset;
		copy->value.limit = period_last->value.limit;
		copy->value.number_phases = period_last->value.number_phases;
	}

	(*copy_count_out)++;

	*offset_end_out = end - start;
	if(profile->charging_schedule.duration != NULL
		&& *offset_end_out > (abs_start + *profile->charging_schedule.duration) - start)
		*offset_end_out = (abs_start + *profile->charging_schedule.duration) - start;

	if(*offset_end_out + start > profile->valid_to)
		*offset_end_out = profile->valid_to - start;

	while(period != NULL && *copy_count_out < max_copies // Copy while input exist and output not exceeded,
		&& period->value.start_period < *offset_end_out + offset_diff){ // Valid offset not exceeded

		struct ocpp_charging_schedule_period_list * new_period = ocpp_extend_period_list(copy, &period->value);

		if(new_period == NULL){
			ESP_LOGE(TAG, "Unable to copy period");
			break;
		}else{
			new_period->value.start_period = period->value.start_period + offset_diff;
			copy = new_period;

			(*copy_count_out)++;

			period_last = period;
			period = period->next;
		}
	}


	if(period != NULL && period->value.start_period + offset_diff < *offset_end_out)
		*offset_end_out = period->value.start_period + offset_diff;

	return ret;
}

/**
 * @brief creates a period list for a given range from transaction start with offset to a given end time.
 *
 * @todo Consider caching profiles instead of rereading when same profile is used to compute multiple separate sections.
 *
 * @return duration of the created period list.
 */
int compute_range(time_t start, int offset, time_t end, int * transaction_id,
		struct ocpp_charging_profile * (*next_profile)(struct ocpp_charging_profile *),
		int max_periods, struct ocpp_charging_schedule_period_list * period_list_out){

	size_t index = 0;
	struct ocpp_charging_profile * current_profile = next_profile(NULL);

	struct ocpp_charging_profile ** profile_stack = calloc(sizeof(struct ocpp_charging_profile), CONFIG_OCPP_CHARGE_PROFILE_MAX_STACK_LEVEL);
	time_t * range_stack = calloc(sizeof(time_t), 2 * CONFIG_OCPP_CHARGE_PROFILE_MAX_STACK_LEVEL);

	if(profile_stack == NULL || range_stack == NULL){
		ESP_LOGE(TAG, "Unable to allocate memory for profile stack or range info");
		goto cleanup;
	}

	bool empty = true;

	while(max_periods > 0 && (start + offset < end)){
		time_t active_from = get_when_schedule_is_active(start, offset, current_profile, transaction_id);

		if(active_from != start + offset){ // If current profile is inactive
			if(active_from < end // If it will be active later in the range
				&& (index == 0 || active_from < range_stack[(index -1) * 2])){ // And is not superseeded by another profile active at the same time.

				// save the profile with active_from and end, where end is when a higher priority profile will be active or range is complete.
				profile_stack[index] = current_profile;
				range_stack[index * 2] = active_from;
				range_stack[(index * 2) +1] = end;

				end = active_from;

				index++;
			}

			// Set current profile lower priority profile.
			current_profile = next_profile(current_profile);

		}else{ // If current profile is active

			int copy_count = 0;
			struct ocpp_charging_schedule_period_list * period_section = copy_period_at(start, offset,  end, current_profile, max_periods,
												&offset, &copy_count);
			if(period_section == NULL){
				ESP_LOGE(TAG, "Unable to copy period");
				goto cleanup;
			}

			if(empty){
				period_list_out->value.start_period = period_section->value.start_period;
				period_list_out->value.limit = period_section->value.limit;
				period_list_out->value.number_phases = period_section->value.number_phases;

				empty = false;
			}

			if(ocpp_period_is_equal_charge(&period_list_out->value, &period_section->value)){
				struct ocpp_charging_schedule_period_list * tmp = period_section;
				period_section = period_section->next;

				copy_count--;

				free(tmp);
			}

			max_periods -= copy_count;

			period_list_out->next = period_section;

			while(period_list_out->next != NULL)
				period_list_out = period_list_out->next;

		}

		while(start + offset >= end && index > 0){ // End for current profile and higher priority profile exist
			// Clear current profile
			ocpp_free_charging_profile(current_profile);

			// Replace it with the higher priority profile
			current_profile = profile_stack[--index];
			end = range_stack[(index *2)+1];

			profile_stack[index] = NULL;
			range_stack[(index *2)] = 0;
			range_stack[(index *2) +1] = 0;
		}
	}

	ESP_LOGI(TAG,  "Range computed: %ld -> %ld offset: %d", start, end, offset);

cleanup:
	while(index > 0)
		ocpp_free_charging_profile(profile_stack[--index]);

	ocpp_free_charging_profile(current_profile);

	free(profile_stack);
	free(range_stack);

	return offset;
}

// Assumes that all profiles are relevant for expected connector id, i.e expect charger to only have 1 connector and profiles to be valid for this charger.
static void get_active_profile(time_t relative_start, int sec_since_start, int * transaction_id, struct ocpp_charging_profile ** profile_out, struct ocpp_charging_profile * (*next_profile)(struct ocpp_charging_profile *), time_t * renewal_time_out){

	*renewal_time_out = LONG_MAX;
	time_t when = relative_start + sec_since_start;

	while(true){ // stack_level -1 is used to indicate default profile if no other exists or active

		struct ocpp_charging_profile * tmp_profile = *profile_out;
		*profile_out = next_profile(tmp_profile);
		ocpp_free_charging_profile(tmp_profile);

		// Find "the prevailing charging profile"
		time_t valid_from = get_when_schedule_is_active(relative_start, sec_since_start, *profile_out, transaction_id);

		if(valid_from == when){
			break;
		}else if(valid_from == LONG_MAX){ // If it will never be active in this context
			if((*profile_out)->valid_to < time(NULL)){ // Check if it can be removed
				remove_profile(*profile_out, false);
			}
		}else{ // If it may be valid later
			if(*renewal_time_out > valid_from){
				*renewal_time_out = valid_from;
			}
		}
	}

	// When we have selected a profile, we check if it will become invalid and need to be renewed due to valid_to
	if(*renewal_time_out > (*profile_out)->valid_to)
		*renewal_time_out = (*profile_out)->valid_to;

	//We also check if it will need to be renewed due to  duration
	if((*profile_out)->charging_schedule.duration != NULL){
		time_t profile_start;
		if(get_normalized_time(relative_start, sec_since_start, *profile_out, &profile_start, NULL) == ESP_OK){

			if(*renewal_time_out > *(*profile_out)->charging_schedule.duration + profile_start)
				*renewal_time_out = *(*profile_out)->charging_schedule.duration + profile_start;
		}
	}
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

bool get_period_from_schedule(struct ocpp_charging_schedule * schedule, uint time_since_start, enum ocpp_recurrency_kind * recurrency_kind, struct ocpp_charging_schedule_period * period_out, float * min_charging_rate_out, uint32_t * time_to_next_period){

	ESP_LOGI(TAG, "Getting period from schedule at %u", time_since_start);
	struct ocpp_charging_schedule_period_list * current_period = &schedule->schedule_period;
	struct ocpp_charging_schedule_period_list * last_period = NULL;

	*min_charging_rate_out = schedule->min_charging_rate;
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

	if(schedule->duration != NULL && time_since_start > *schedule->duration){
		ESP_LOGE(TAG, "Schedule is invalid at requested offset from start");
		return false;
	}

	while(true){
		if(current_period == NULL || current_period->value.start_period > time_since_start){
			if(last_period != NULL){
				period_out->start_period = last_period->value.start_period;
				period_out->limit = last_period->value.limit;
				period_out->number_phases = last_period->value.number_phases;
			}else{
				ESP_LOGE(TAG, "Invalid schedule start or time offset (%u)", time_since_start);
				return false;
			}

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

/**
 * @brief Combines two schedules. It assumes that both schedules have the same schedule_start.
 */
void combine_schedules(struct ocpp_charging_schedule * schedule1, struct ocpp_charging_schedule * schedule2, struct ocpp_charging_schedule * schedule_out){
	ESP_LOGI(TAG, "Combining schedules");

	int duration = INT_MAX;

	// Limit the outputs duration to the shortest duration of the inputs
	if(schedule1->duration != NULL || schedule2->duration != NULL){
		schedule_out->duration = malloc(sizeof(int));
		if(schedule_out->duration == NULL){
			ESP_LOGE(TAG, "Unable to allocate duration for composite schedule");
			return;
		}

		*schedule_out->duration = INT_MAX;

		if(schedule1->duration != NULL)
			*schedule_out->duration = *schedule1->duration;

		if(schedule2->duration != NULL && *schedule2->duration < *schedule_out->duration)
			*schedule_out->duration = *schedule2->duration;

		duration = *schedule_out->duration;
	}

	schedule_out->start_schedule = malloc(sizeof(time_t));
	if(schedule_out->start_schedule == NULL){
		ESP_LOGE(TAG, "Unable to start schedule for composite schedule");
		return;
	}
	*schedule_out->start_schedule = *schedule1->start_schedule;

	schedule_out->charge_rate_unit = eOCPP_CHARGING_RATE_A;
	schedule_out->min_charging_rate = (schedule1->min_charging_rate < schedule2->min_charging_rate) ? schedule1->min_charging_rate : schedule2->min_charging_rate;

	struct ocpp_charging_schedule_period_list * list1 = &schedule1->schedule_period;
	struct ocpp_charging_schedule_period_list * list2 = &schedule2->schedule_period;

	bool empty = true;

	while(true){

		struct ocpp_charging_schedule_period new_period;

		if(list1->value.start_period > list2->value.start_period){ // set start_period out to last latest active start_period
			new_period.start_period = list1->value.start_period;
		}else{
			new_period.start_period = list2->value.start_period;
		}

		if(new_period.start_period > duration) // End if schedule would no longer be valid
			return;

		if(list1->value.limit < list2->value.limit){ // Set limit to the minimum of the schedules to combine
			new_period.limit = list1->value.limit;
		}else{
			new_period.limit = list2->value.limit;
		}

		if(list1->value.number_phases < list2->value.number_phases){ // Set number_phase to minimum
			new_period.number_phases = list1->value.number_phases;
		}else{
			new_period.number_phases = list2->value.number_phases;
		}

		if(empty){
			schedule_out->schedule_period.value.start_period = new_period.start_period;
			schedule_out->schedule_period.value.limit = new_period.limit;
			schedule_out->schedule_period.value.number_phases = new_period.number_phases;

			empty = false;
		}else{
			ocpp_extend_period_list(&schedule_out->schedule_period, &new_period);
		}

		if(list1->next != NULL && list2->next != NULL){
			if(list1->next->value.start_period == list2->next->value.start_period){
				list1 = list1->next;
				list2 = list2->next;

			}else if(list1->next->value.start_period < list2->next->value.start_period){
				list1 = list1->next;

			}else{
				list2 = list2->next;
			}

		}else if(list1->next == NULL && list2->next == NULL){
			break;
		}else{
			if(list1->next != NULL){
				list1 = list1->next;
			}else{
				list2 = list2->next;
			}
		}
	}
}

//Assumes that the profiles are valid at relative_start + sec_since_start
static esp_err_t create_composite_schedule(time_t relative_start, int sec_since_start,
					struct ocpp_charging_profile * profile_tx, struct ocpp_charging_profile * profile_max,
					struct ocpp_charging_schedule * schedule_out){

	ESP_LOGI(TAG, "Creating composite schedule");

	struct ocpp_charging_schedule tx_schedule = {0};
	struct ocpp_charging_schedule max_schedule = {0};

	int offset_tx = 0;
	int offset_max = 0;

	int copy_count = 0;
	struct ocpp_charging_schedule_period_list * periods = copy_period_at(relative_start, sec_since_start, LONG_MAX, profile_tx, CONFIG_OCPP_CHARGING_SCHEDULE_MAX_PERIODS, &offset_tx, &copy_count);
	if(periods == NULL){
		ESP_LOGE(TAG, "Unable to copy period when creating composite schedule");
		return ESP_FAIL;
	}

	tx_schedule.schedule_period.value.start_period = periods->value.start_period;
	tx_schedule.schedule_period.value.limit = periods->value.limit;
	tx_schedule.schedule_period.value.number_phases = periods->value.number_phases;
	tx_schedule.schedule_period.next = periods->next;
	free(periods);

	copy_count = 0;
	periods = copy_period_at(relative_start, sec_since_start, LONG_MAX, profile_max, CONFIG_OCPP_CHARGING_SCHEDULE_MAX_PERIODS, &offset_max, &copy_count);
	if(periods == NULL){
		ESP_LOGE(TAG, "Unable to copy period when creating composite schedule");
		return ESP_FAIL;
	}

	max_schedule.schedule_period.value.start_period = periods->value.start_period;
	max_schedule.schedule_period.value.limit = periods->value.limit;
	max_schedule.schedule_period.value.number_phases = periods->value.number_phases;
	max_schedule.schedule_period.next = periods->next;
	free(periods);

	if(tx_schedule.start_schedule == NULL)
		tx_schedule.start_schedule = malloc(sizeof(time_t));

	if(max_schedule.start_schedule == NULL)
		max_schedule.start_schedule = malloc(sizeof(time_t));

	if(tx_schedule.start_schedule == NULL || max_schedule.start_schedule == NULL){
		ESP_LOGE(TAG, "Unable to allocate start schedule for tx of max schedule during creation schedule");
		return ESP_ERR_NO_MEM;
	}

	*tx_schedule.start_schedule = relative_start + sec_since_start;
	*max_schedule.start_schedule = relative_start + sec_since_start;


	tx_schedule.min_charging_rate = profile_tx->charging_schedule.min_charging_rate;
	max_schedule.min_charging_rate = profile_max->charging_schedule.min_charging_rate;

	combine_schedules(&tx_schedule, &max_schedule, schedule_out);

	schedule_out->schedule_period.value.start_period = 0; // Ensures valid schedule from 0 if sec_since_start > 0

	ocpp_free_charging_schedule(&tx_schedule, false);
	ocpp_free_charging_schedule(&max_schedule, false);

	return ESP_OK;
}

void get_composite_schedule_cb(const char * unique_id, const char * action, cJSON * payload, void * cb_data){
	ESP_LOGI(TAG, "Received request for get composite schedule");

	char err_str[124] = {0};

	struct ocpp_charging_schedule tx_schedule = {0};
	struct ocpp_charging_schedule max_schedule = {0};

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

	err = ocppj_get_string_field(payload, "chargingRateUnit", false, &charging_rate_unit, err_str, sizeof(err_str));
	if(err != eOCPPJ_NO_VALUE){

		if(err != eOCPPJ_NO_ERROR){
			ESP_LOGW(TAG, "Invalid chargingRateUnit in request: '%s'", err_str);
			goto error;
		}

		if(ocpp_validate_enum(charging_rate_unit, true, 1,
					OCPP_CHARGING_RATE_A,
					OCPP_CHARGING_RATE_W) != 0){

			err = eOCPPJ_ERROR_PROPERTY_CONSTRAINT_VIOLATION;
			strcpy(err_str, "Expected 'chargingRateUnit' to be ChargingRateUnit type");
			goto error;

		}

		if(!ocpp_csl_contains(ocpp_get_allowed_charging_rate_units(), charging_rate_unit)){
			err = eOCPPJ_ERROR_NOT_SUPPORTED;
			strcpy(err_str, "'Requested chargingRateUnit' is not supported");
			goto error;
		}
	}

	tx_schedule.duration = malloc(sizeof(int));
	max_schedule.duration = malloc(sizeof(int));

	if(tx_schedule.duration == NULL || max_schedule.duration == NULL){
		ESP_LOGE(TAG, "Unable to allocate duration for requested composite schedule");
		goto error;
	}

	tx_schedule.start_schedule = malloc(sizeof(time_t));
	max_schedule.start_schedule = malloc(sizeof(time_t));

	if(tx_schedule.start_schedule == NULL || max_schedule.start_schedule == NULL){
		ESP_LOGE(TAG, "Unable to allocate start schedule for requested composite schedule");
		goto error;
	}

	time_t start_time = time(NULL);

	*tx_schedule.start_schedule = start_time;
	*max_schedule.start_schedule = start_time;

	*tx_schedule.duration = compute_range(start_time, 0, start_time+duration, NULL,
					next_tx_or_tx_default_profile, CONFIG_OCPP_CHARGING_SCHEDULE_MAX_PERIODS, &tx_schedule.schedule_period);

	*max_schedule.duration = compute_range(start_time, 0, start_time+duration, NULL,
					next_max_profile, CONFIG_OCPP_CHARGING_SCHEDULE_MAX_PERIODS, &max_schedule.schedule_period);

	struct ocpp_charging_schedule composite_schedule = {0};
	combine_schedules(&tx_schedule, &max_schedule, &composite_schedule);

	/*
	 * errata v4.0 states: "When ChargingSchedule is used as part of a GetCompositeSchedule.conf message, then [StartSchedule] field must be omitted."
	 */
	if(composite_schedule.start_schedule != NULL){
		free(composite_schedule.start_schedule);
		composite_schedule.start_schedule = NULL;
	}

	cJSON * reply = ocpp_create_get_composite_schedule_confirmation(unique_id, OCPP_GET_COMPOSITE_SCHEDULE_STATUS_ACCEPTED,
									&connector_id, &start_time, &composite_schedule);

	ocpp_free_charging_schedule(&tx_schedule, false);
	ocpp_free_charging_schedule(&max_schedule, false);
	ocpp_free_charging_schedule(&composite_schedule, false);

	if(reply != NULL){
		send_call_reply(reply);
	}else{
		ESP_LOGE(TAG, "Unable to create ocpp error for not implemented");
		err = eOCPPJ_ERROR_INTERNAL;
		sprintf(err_str, "Error occured while attempting to create GetCompositeSchedule.conf");
		goto error;
	}

	return;

error:
	if(err == eOCPPJ_NO_ERROR){
		ESP_LOGE(TAG, "Unknown error occured during get composite schedule");
		err = eOCPPJ_ERROR_INTERNAL;
	}

	ocpp_free_charging_schedule(&tx_schedule, false);
	ocpp_free_charging_schedule(&max_schedule, false);

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
	time_t renewal_time_period = LONG_MAX;

	// Time until next predicted change event
	uint32_t next_renewal_delay = UINT32_MAX;

	transaction_is_active = false;

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
				}else{
					ESP_LOGW(TAG, "Transaction_id not set, expecting no transaction");
					transaction_is_active = false; // Could be wrong in offline or if delay between start and setting id
					remove_profile_from_memory(NULL, NULL, true);
				}
			}else{
				transaction_is_active = true;
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
			ESP_LOGI(TAG, "Handling active transaction. start: %ld, offset %ld", transaction_start_time, current_time - transaction_start_time);
			if(profile_tx == NULL || renewal_time_tx < current_time){
				ESP_LOGI(TAG, "Current tx profile timed out");
				data |= eACTIVE_PROFILE_TX_CHANGE;
			}

			if(profile_max == NULL || renewal_time_max < current_time){
				ESP_LOGI(TAG, "Current max profile timed out");
				data |= eACTIVE_PROFILE_MAX_CHANGE;
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

				get_active_profile(transaction_start_time, current_time - transaction_start_time, active_transaction_id,
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


				get_active_profile(transaction_start_time, current_time - transaction_start_time, active_transaction_id,
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

				if(schedule == NULL)
					schedule = calloc(sizeof(struct ocpp_charging_schedule), 1);

				if(schedule == NULL){
					ESP_LOGE(TAG, "Unable to allocate memory for composite schedule");
					goto cleanup;
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

				ocpp_free_charging_schedule_period_list(schedule->schedule_period.next);
				schedule->schedule_period.next = NULL;

				create_composite_schedule(transaction_start_time, current_time - transaction_start_time, profile_tx, profile_max, schedule);

				current_min = schedule->min_charging_rate;
				data |= eACTIVE_PERIOD_CHANGE;
			}

			if(data & eACTIVE_PERIOD_CHANGE){
				ESP_LOGI(TAG, "Updating period");
				uint32_t renewal_delay_period;
				if(!get_period_from_schedule(schedule, current_time - *schedule->start_schedule, NULL,
				 				&current_period, &current_min, &renewal_delay_period)){

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

			if(last_min != current_min || !ocpp_period_is_equal_charge(&current_period, &last_period)){

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

cleanup:
	ESP_LOGW(TAG, "Smart charging exited");
	ocpp_free_charging_profile(profile_tx); // tx or txDefault profile
	ocpp_free_charging_profile(profile_max);
	ocpp_free_charging_schedule(schedule, true);

	ocpp_smart_task_handle = NULL;

	vTaskDelete(NULL);
}

esp_err_t ocpp_smart_charging_init(){
	ESP_LOGI(TAG, "Initializing smart charging");

	if(strcmp(ocpp_get_allowed_charging_rate_units(), "A") != 0){
		ESP_LOGE(TAG, "Configuration is set to unsupported charge rate unit '%s'",
			ocpp_get_allowed_charging_rate_units());
		return ESP_ERR_NOT_SUPPORTED;
	}

	file_lock = xSemaphoreCreateMutex();
	if(file_lock == NULL){
		ESP_LOGE(TAG, "Unable to create mutex");
		return ESP_ERR_NO_MEM;
	}

	struct stat st;
	if(stat(base_path, &st) != 0){
		ESP_LOGE(TAG, "'%s' not mounted", base_path);
		xSemaphoreGive(file_lock);
		return ESP_ERR_INVALID_STATE;
	}

	tx_profiles = calloc(sizeof(struct ocpp_charging_profile *), CONFIG_OCPP_CHARGE_PROFILE_MAX_STACK_LEVEL+1);
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

#define MAX_DEINIT_WAIT 5000
void ocpp_smart_charging_deinit(){

	if(ocpp_smart_task_handle == NULL){
		ESP_LOGW(TAG, "Requested to deinit ocpp smart charging, but no handle exists");
		return;
	}

	time_t deinit_begin = time(NULL);

	xTaskNotify(ocpp_smart_task_handle, eNOT_NEEDED, eSetBits);

	bool complete = false;

	while(deinit_begin + MAX_DEINIT_WAIT > time(NULL)){
		if(ocpp_smart_task_handle == NULL){
			complete = true;
			break;
		}

		vTaskDelay(pdMS_TO_TICKS(1000));
	}

	if(complete){
		ESP_LOGI(TAG, "ocpp smart charging task successfully removed");
	}else{
		ESP_LOGE(TAG, "Unable to remove ocpp smart charging task.");
	}
}

void ocpp_set_on_new_period_cb(void (* on_new_period)(float min_charging_limit, float max_charging_limit, uint8_t number_phases)){
	charge_value_cb = on_new_period;
}

cJSON * ocpp_smart_get_diagnostics(){
	cJSON * res = cJSON_CreateObject();
	if(res == NULL){
		ESP_LOGE(TAG, "Unable to create ocpp diagnostics");
		return res;
	}

	cJSON_AddNumberToObject(res, "active_transaction_id", active_transaction_id != NULL ? *active_transaction_id : -2);

	if(tx_profiles != NULL){
		cJSON * active_tx_profile_indexes = cJSON_CreateArray();
		if(active_tx_profile_indexes != NULL){
			for(size_t i = 0; i < CONFIG_OCPP_CHARGE_PROFILE_MAX_STACK_LEVEL+1; i++){
				if(tx_profiles[i] != NULL){
					cJSON_AddItemToArray(active_tx_profile_indexes, cJSON_CreateNumber(i));
				}
			}
			cJSON_AddItemToObject(res, "tx_profiles", active_tx_profile_indexes);
		}
	}

	return NULL;
}
