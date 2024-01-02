#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "esp_log.h"

#include "types/ocpp_charging_profile.h"
#include "types/ocpp_date_time.h"
#include "types/ocpp_enum.h"
#include "types/ocpp_csl.h"
#include "ocpp_json/ocppj_validation.h"

static const char * TAG = "OCPP CHARGING";

struct ocpp_charging_schedule_period_list * ocpp_extend_period_list(struct ocpp_charging_schedule_period_list * period_list, struct ocpp_charging_schedule_period * period){
	while(period_list->next != NULL)
		period_list = period_list->next;

	if(ocpp_period_is_equal_charge(&period_list->value, period)){ // period can be merged with last period in list
		return period_list;
	}

	period_list->next = calloc(sizeof(struct ocpp_charging_schedule_period_list), 1);

	period_list = period_list->next;

	if(period_list != NULL){
		period_list->value.start_period = period->start_period;
		period_list->value.limit = period->limit;
		period_list->value.number_phases = period->number_phases;
	}

	return period_list;
}

enum ocppj_err_t charging_schedule_period_from_json(cJSON * chargingSchedulePeriod, struct ocpp_charging_schedule_period * period_out,
						char * error_description_out, size_t error_description_length){

	enum ocppj_err_t ocppj_error = ocppj_get_int_field(chargingSchedulePeriod, "startPeriod", true, &period_out->start_period,
							error_description_out, error_description_length);

	if(ocppj_error != eOCPPJ_NO_ERROR)
		return ocppj_error;

	double limit;
	ocppj_error = ocppj_get_decimal_field(chargingSchedulePeriod, "limit", true, &limit,
					error_description_out, error_description_length);

	if(ocppj_error != eOCPPJ_NO_ERROR)
		return ocppj_error;

	if(limit < 0 || limit > 80){
		snprintf(error_description_out, error_description_length, "'limit' out of range. IEC 61851-1 Expects 0-80");
		return eOCPPJ_ERROR_PROPERTY_CONSTRAINT_VIOLATION;
	}

	period_out->limit = (float)limit;

	ocppj_error = ocppj_get_int_field(chargingSchedulePeriod, "numberPhases", false, &period_out->number_phases,
					error_description_out, error_description_length);

	if(ocppj_error == eOCPPJ_NO_VALUE){
		period_out->number_phases = 3; //"numberPhases=3 will be assumed unless another number is given."

	}else if(ocppj_error != eOCPPJ_NO_ERROR){
		return ocppj_error;

	}

	if(period_out->number_phases != 1 && period_out->number_phases != 3){
		snprintf(error_description_out, error_description_length, "Unexpected 'numberPhases' value. Expected 1 or 3 phases");
		return eOCPPJ_ERROR_PROPERTY_CONSTRAINT_VIOLATION;
	}

	return eOCPPJ_NO_ERROR;
}

enum ocppj_err_t charging_schedule_from_json(cJSON * chargingSchedule, const char * allowed_charging_rate_units,
					int max_periods, enum ocpp_charging_profile_kind profile_kind,
					struct ocpp_charging_schedule * charging_schedule_out,
					char * error_description_out, size_t error_description_length){

	int duration;
	enum ocppj_err_t ocppj_error = ocppj_get_int_field(chargingSchedule, "duration", false, &duration,
							error_description_out, error_description_length);

	if(ocppj_error == eOCPPJ_NO_ERROR){
		charging_schedule_out->duration = malloc(sizeof(int));
		if(charging_schedule_out->duration == NULL){
			snprintf(error_description_out, error_description_length, "Unable to allocate memory for duration");
			return eOCPPJ_ERROR_INTERNAL;
		}

		*charging_schedule_out->duration = duration;

	}else if(ocppj_error != eOCPPJ_NO_VALUE){
		return ocppj_error;
	}

	char * value_str;
	switch(profile_kind){
	case eOCPP_CHARGING_PROFILE_KIND_ABSOLUTE:
	case eOCPP_CHARGING_PROFILE_KIND_RECURRING: // TODO: check if startSchedule should be optional for recurring.
		ocppj_error = ocppj_get_string_field(chargingSchedule, "startSchedule", true, &value_str,
						error_description_out, error_description_length);
		break;
	case eOCPP_CHARGING_PROFILE_KIND_RELATIVE:
		if(cJSON_HasObjectItem(chargingSchedule, "startSchedule")){
			snprintf(error_description_out, error_description_length, "Unexpected 'startSchedule' for Relative charge profile");
			return eOCPPJ_ERROR_FORMATION_VIOLATION;
		}else{
			ocppj_error = eOCPPJ_NO_VALUE;
		}
	}

	if(ocppj_error != eOCPPJ_NO_ERROR && ocppj_error != eOCPPJ_NO_VALUE)
		return ocppj_error;

	if(ocppj_error == eOCPPJ_NO_ERROR){
		time_t start_schedule = ocpp_parse_date_time(value_str);

		if(start_schedule == (time_t)-1){
			snprintf(error_description_out, error_description_length, "'startSchedule' Unrecognised dateTime format");
			return eOCPPJ_ERROR_TYPE_CONSTRAINT_VIOLATION;
		}

		charging_schedule_out->start_schedule = malloc(sizeof(time_t));
		if(charging_schedule_out->start_schedule == NULL)
		{
			snprintf(error_description_out, error_description_length, "Unable to allocate memory for startSchedule");
			return eOCPPJ_ERROR_INTERNAL;
		}

		*charging_schedule_out->start_schedule = start_schedule;
	}

	ocppj_error = ocppj_get_string_field(chargingSchedule, "chargingRateUnit", true, &value_str,
					error_description_out, error_description_length);

	if(ocppj_error != eOCPPJ_NO_ERROR)
		return ocppj_error;

	if(ocpp_validate_enum(value_str, true, 2,
				OCPP_CHARGING_RATE_W,
				OCPP_CHARGING_RATE_A) == 0){

		if(ocpp_csl_contains(allowed_charging_rate_units, value_str)){

			charging_schedule_out->charge_rate_unit = ocpp_charging_rate_unit_to_id(value_str);
		}else{
			snprintf(error_description_out, error_description_length, "'chargingRateUnit' Is valid but not supported by current firmware");
			return eOCPPJ_ERROR_NOT_SUPPORTED;
		}
	}else{
		snprintf(error_description_out, error_description_length, "Expected 'chargingRateUnit' to be ChargingRateUnitType type");
		return eOCPPJ_ERROR_PROPERTY_CONSTRAINT_VIOLATION;
	}

	if(cJSON_HasObjectItem(chargingSchedule, "chargingSchedulePeriod")){

		cJSON * periods_json = cJSON_GetObjectItem(chargingSchedule, "chargingSchedulePeriod");
		if(cJSON_IsArray(periods_json)){

			int periods_count = cJSON_GetArraySize(periods_json);
			if(periods_count > 0 && periods_count <= max_periods){

				struct ocpp_charging_schedule_period_list * entry = &(charging_schedule_out->schedule_period);
				for(int i = 0; i < periods_count; i++){
					enum ocppj_err_t result = charging_schedule_period_from_json(cJSON_GetArrayItem(periods_json, i),
												&entry->value, error_description_out,
												error_description_length);
					if(result != eOCPPJ_NO_ERROR)
						return result;

					if(i+1 < periods_count){
						entry->next = malloc(sizeof(struct ocpp_charging_schedule_period_list));
						if(entry->next == NULL){
							snprintf(error_description_out, error_description_length, "Unable to allocate memory for period");
							return eOCPPJ_ERROR_INTERNAL;
						}

						entry = entry->next;
						entry->next = NULL;
					}
				}
			}else{
				snprintf(error_description_out, error_description_length, "'chargingSchedulePeriod' Array size out of range");
				return eOCPPJ_ERROR_PROPERTY_CONSTRAINT_VIOLATION;
			}
		}else{
			snprintf(error_description_out, error_description_length, "Expected 'chargingSchedulePeriod' to be array type");
			return eOCPPJ_ERROR_TYPE_CONSTRAINT_VIOLATION;
		}
	}else{
		snprintf(error_description_out, error_description_length, "Expected 'chargingSchedulePeriod' field");
		return eOCPPJ_ERROR_FORMATION_VIOLATION;
	}

	double minimum;
	ocppj_error = ocppj_get_decimal_field(chargingSchedule, "minChargingRate", false, &minimum,
					error_description_out, error_description_length);
	if(ocppj_error == eOCPPJ_NO_ERROR){

		if(minimum < 0 || minimum > 32){
			snprintf(error_description_out, error_description_length, "'minChargingRate' out of range. Expected 0-32");
			return eOCPPJ_ERROR_PROPERTY_CONSTRAINT_VIOLATION;
		}

		charging_schedule_out->min_charging_rate = (float)minimum;

	}else if(ocppj_error == eOCPPJ_NO_VALUE){
		charging_schedule_out->min_charging_rate = 6.0f;

	}else{
		return ocppj_error;
	}

	return eOCPPJ_NO_ERROR;
}

enum ocppj_err_t ocpp_charging_profile_from_json(cJSON * csChargingProfiles, int max_stack_level, const char * allowed_charging_rate_units,
				int max_periods, struct ocpp_charging_profile * charging_profile_out,
				char * error_description_out, size_t error_description_length){

	if(!cJSON_IsObject(csChargingProfiles)){
			enum ocppj_err_t ret;
			if(cJSON_IsNull(csChargingProfiles)){
					snprintf(error_description_out, error_description_length, "Expected 'ChargingProfile' object was missing");
					ret = eOCPPJ_ERROR_FORMATION_VIOLATION;
			}else{
					snprintf(error_description_out, error_description_length, "Expected 'ChargingProfile' object, but had incorrect type");
					ret = eOCPPJ_ERROR_TYPE_CONSTRAINT_VIOLATION;
			}
			return ret;
	}

	enum ocppj_err_t ocppj_error = ocppj_get_int_field(csChargingProfiles, "chargingProfileId", true,
							&charging_profile_out->profile_id,
							error_description_out, error_description_length);
	if(ocppj_error != eOCPPJ_NO_ERROR)
		return ocppj_error;

	ocppj_error = ocppj_get_int_field(csChargingProfiles, "stackLevel", true, &charging_profile_out->stack_level,
					error_description_out, error_description_length);
	if(ocppj_error != eOCPPJ_NO_ERROR)
		return ocppj_error;

	if(charging_profile_out->stack_level > max_stack_level
		|| charging_profile_out->stack_level < 0){

		snprintf(error_description_out, error_description_length, "'stackLevel' is out of range");
		return eOCPPJ_ERROR_PROPERTY_CONSTRAINT_VIOLATION;
	}

	char * enum_str;
	ocppj_error = ocppj_get_string_field(csChargingProfiles, "chargingProfilePurpose", true, &enum_str,
					error_description_out, error_description_length);
	if(ocppj_error != eOCPPJ_NO_ERROR)
		return ocppj_error;

	if(ocpp_validate_enum(enum_str, true, 3,
				OCPP_CHARGING_PROFILE_PURPOSE_CHARGE_POINT_MAX,
				OCPP_CHARGING_PROFILE_PURPOSE_TX_DEFAULT,
				OCPP_CHARGING_PROFILE_PURPOSE_TX) == 0){

		charging_profile_out->profile_purpose = ocpp_charging_profile_purpose_to_id(enum_str);
	}else{
		snprintf(error_description_out, error_description_length, "Expected 'chargingProfilePurpose' to be ChargingProfilePurposeType");
		return eOCPPJ_ERROR_TYPE_CONSTRAINT_VIOLATION;
	}

	if(charging_profile_out->profile_purpose == eOCPP_CHARGING_PROFILE_PURPOSE_TX){
		int transaction_id;
		ocppj_error = ocppj_get_int_field(csChargingProfiles, "transactionId", false, &transaction_id,
						error_description_out, error_description_length);

		if(ocppj_error == eOCPPJ_NO_ERROR){
			charging_profile_out->transaction_id = malloc(sizeof(int));
			if(charging_profile_out->transaction_id == NULL){
				snprintf(error_description_out, error_description_length, "Unable to allocate memory for transactionId");
				return eOCPPJ_ERROR_INTERNAL;
			}else{
				*charging_profile_out->transaction_id = transaction_id;
			}
		}else if(ocppj_error != eOCPPJ_NO_VALUE){
			return ocppj_error;
		}

	}else if(cJSON_HasObjectItem(csChargingProfiles, "transactionId")){
		snprintf(error_description_out, error_description_length, "Profile contains transactionId, but chargingProfilePurpose is not 'TxProfile'");
		return eOCPPJ_ERROR_FORMATION_VIOLATION;
	}

	ocppj_error = ocppj_get_string_field(csChargingProfiles, "chargingProfileKind", true, &enum_str,
					error_description_out, error_description_length);
	if(ocppj_error != eOCPPJ_NO_ERROR){
		return ocppj_error;

	}else if(ocpp_validate_enum(enum_str, true, 3,
					OCPP_CHARGING_PROFILE_KIND_ABSOLUTE,
					OCPP_CHARGING_PROFILE_KIND_RECURRING,
					OCPP_CHARGING_PROFILE_KIND_RELATIVE) == 0){

		charging_profile_out->profile_kind = ocpp_charging_profile_kind_to_id(enum_str);
	}else{
		snprintf(error_description_out, error_description_length, "Expected 'chargingProfileKind' to be ChargingProfileKindType");
		return eOCPPJ_ERROR_TYPE_CONSTRAINT_VIOLATION;
	}

	if(charging_profile_out->profile_kind == eOCPP_CHARGING_PROFILE_KIND_RECURRING){
		ocppj_error = ocppj_get_string_field(csChargingProfiles, "recurrencyKind", true, &enum_str,
						error_description_out, error_description_length);
		if(ocppj_error != eOCPPJ_NO_ERROR){
			return ocppj_error;

		}else if(ocpp_validate_enum(enum_str, true, 2,
						OCPP_RECURRENCY_KIND_DAILY,
						OCPP_RECURRENCY_KIND_WEEKLY) == 0){

			charging_profile_out->recurrency_kind = malloc(sizeof(enum ocpp_recurrency_kind));
			if(charging_profile_out->recurrency_kind == NULL){
				snprintf(error_description_out, error_description_length, "Unable to allocate memory for 'recurrencyKind'");
				return eOCPPJ_ERROR_INTERNAL;
			}

			*charging_profile_out->recurrency_kind = ocpp_recurrency_kind_to_id(enum_str);
		}else{
			snprintf(error_description_out, error_description_length, "Expected 'recurrencyKind' to be RecurrencyKindType");
			return eOCPPJ_ERROR_TYPE_CONSTRAINT_VIOLATION;
		}
	}else{
		if(cJSON_HasObjectItem(csChargingProfiles, "recurrencyKind")){
			snprintf(error_description_out, error_description_length, "Profile contains recurrencyKind, but chargingProfileKind is not 'Recurring'");
			return eOCPPJ_ERROR_FORMATION_VIOLATION;
		}
	}

	char * date_time_str;
	ocppj_error = ocppj_get_string_field(csChargingProfiles, "validFrom", false, &date_time_str,
					error_description_out, error_description_length);

	if(ocppj_error == eOCPPJ_NO_ERROR){
		charging_profile_out->valid_from = ocpp_parse_date_time(date_time_str);
		if(charging_profile_out->valid_from == (time_t) -1){

			snprintf(error_description_out, error_description_length, "'validFrom' is not a recognoised dateTime");
			return eOCPPJ_ERROR_TYPE_CONSTRAINT_VIOLATION;
		}
	}else if(ocppj_error == eOCPPJ_NO_VALUE){
		charging_profile_out->valid_from = time(NULL); // "If absent, the profile is valid as soon as it is received"

	}else{
		return ocppj_error;
	}

	ocppj_error = ocppj_get_string_field(csChargingProfiles, "validTo", false, &date_time_str,
					error_description_out, error_description_length);
	if(ocppj_error == eOCPPJ_NO_ERROR){
		charging_profile_out->valid_to = ocpp_parse_date_time(date_time_str);
		if(charging_profile_out->valid_to == (time_t) -1){
			snprintf(error_description_out, error_description_length, "'validTo' is not a recognoised dateTime");
			return eOCPPJ_ERROR_TYPE_CONSTRAINT_VIOLATION;
		}


	}else if(ocppj_error == eOCPPJ_NO_VALUE){
		charging_profile_out->valid_to = LONG_MAX;

	}else{
		return ocppj_error;
	}

	if(cJSON_HasObjectItem(csChargingProfiles, "chargingSchedule")){
		return charging_schedule_from_json(cJSON_GetObjectItem(csChargingProfiles, "chargingSchedule"),
						allowed_charging_rate_units, max_periods, charging_profile_out->profile_kind,
						&charging_profile_out->charging_schedule,
						error_description_out, error_description_length);
	}else{
		snprintf(error_description_out, error_description_length, "Expected 'chargingSchedule' field");
		return eOCPPJ_ERROR_FORMATION_VIOLATION;
	}
}

void ocpp_free_charging_schedule_period_list(struct ocpp_charging_schedule_period_list * periods){
	while(periods != NULL){
		struct ocpp_charging_schedule_period_list * tmp = periods->next;
		free(periods);
		periods = tmp;
	}
}

void ocpp_free_charging_schedule(struct ocpp_charging_schedule * charging_schedule, bool with_reference){
	if(charging_schedule == NULL)
		return;

	ocpp_free_charging_schedule_period_list(charging_schedule->schedule_period.next);

	free(charging_schedule->start_schedule);
	free(charging_schedule->duration);

	if(with_reference)
		free(charging_schedule);
}

struct ocpp_charging_schedule_period_list * ocpp_duplicate_charging_schedule_period_list(const struct ocpp_charging_schedule_period_list * period_list){
	struct ocpp_charging_schedule_period_list * result = NULL;
	struct ocpp_charging_schedule_period_list * result_tail = NULL;

	while(period_list != NULL){
		if(result_tail == NULL){
			result_tail = calloc(sizeof(struct ocpp_charging_schedule_period_list), 1);
			result = result_tail;
		}else{
			result_tail->next = calloc(sizeof(struct ocpp_charging_schedule_period_list), 1);
			result_tail = result_tail->next;
		}

		if(result_tail == NULL){
			ESP_LOGE(TAG, "Unable to allocate memory for duplicating schedule_period->next");
			goto error;
		}

		result_tail->value.start_period = period_list->value.start_period;
		result_tail->value.limit = period_list->value.limit;
		result_tail->value.number_phases = period_list->value.number_phases;

		period_list = period_list->next;
	}

	return result;

error:
	 ocpp_free_charging_schedule_period_list(result);
	 return NULL;
}

struct ocpp_charging_profile * ocpp_duplicate_charging_profile(const struct ocpp_charging_profile * profile){
	if(profile == NULL)
		return NULL;

	struct ocpp_charging_profile * duplicate = malloc(sizeof(struct ocpp_charging_profile));
	if(duplicate == NULL){
		ESP_LOGE(TAG, "Unable to allocate memory for duplicate charging profile");
		goto error;
	}

	memcpy(duplicate, profile, sizeof(struct ocpp_charging_profile));

	duplicate->transaction_id = NULL;
	duplicate->recurrency_kind = NULL;
	duplicate->charging_schedule.duration = NULL;
	duplicate->charging_schedule.start_schedule = NULL;
	duplicate->charging_schedule.schedule_period.next = NULL;

	if(profile->transaction_id != NULL){
		duplicate->transaction_id = malloc(sizeof(int));
		if(duplicate->transaction_id == NULL){
			ESP_LOGE(TAG, "Unable to allocate memory for duplicate transaction_id");
			goto error;
		}

		*duplicate->transaction_id = *profile->transaction_id;
	}

	if(profile->recurrency_kind != NULL){
		duplicate->recurrency_kind = malloc(sizeof(enum ocpp_recurrency_kind));
		if(duplicate->recurrency_kind == NULL){
			ESP_LOGE(TAG, "Unable to allocate memory for duplicate recurrency_kind");
			goto error;
		}

		*duplicate->recurrency_kind = *profile->recurrency_kind;
	}

	if(profile->charging_schedule.duration != NULL){
		duplicate->charging_schedule.duration = malloc(sizeof(int));
		if(duplicate->charging_schedule.duration == NULL){
			ESP_LOGE(TAG, "Unable to allocate memory for duplicate duration");
			goto error;
		}

		*duplicate->charging_schedule.duration = *profile->charging_schedule.duration;
	}

	if(profile->charging_schedule.start_schedule != NULL){
		duplicate->charging_schedule.start_schedule = malloc(sizeof(time_t));
		if(duplicate->charging_schedule.start_schedule == NULL){
			ESP_LOGE(TAG, "Unable to allocate memory for duplicate start_schedule");
			goto error;
		}

		*duplicate->charging_schedule.start_schedule = *profile->charging_schedule.start_schedule;
	}

	if(profile->charging_schedule.schedule_period.next != NULL){
		duplicate->charging_schedule.schedule_period.next = ocpp_duplicate_charging_schedule_period_list(profile->charging_schedule.schedule_period.next);
		if(duplicate->charging_schedule.schedule_period.next == NULL){
			ESP_LOGE(TAG, "Unable to get schedule_period for duplicate profile");
			goto error;
		}
	}

	return duplicate;
error:
	return NULL;
}

void ocpp_free_charging_profile(struct ocpp_charging_profile * charging_profile){
	if(charging_profile == NULL || charging_profile->stack_level == -1) // Only delete if it exists and is not default profile
		return;

	ocpp_free_charging_schedule(&charging_profile->charging_schedule, false);

	free(charging_profile->recurrency_kind);
	free(charging_profile->transaction_id);

	free(charging_profile);
}

enum ocpp_charging_profile_purpose ocpp_charging_profile_purpose_to_id(const char * purpose){
	if(strcmp(purpose, OCPP_CHARGING_PROFILE_PURPOSE_CHARGE_POINT_MAX) == 0){
		return eOCPP_CHARGING_PROFILE_PURPOSE_CHARGE_POINT_MAX;

	}else if(strcmp(purpose, OCPP_CHARGING_PROFILE_PURPOSE_TX_DEFAULT) == 0){
		return eOCPP_CHARGING_PROFILE_PURPOSE_TX_DEFAULT;

	}else if(strcmp(purpose, OCPP_CHARGING_PROFILE_PURPOSE_TX) == 0){
		return eOCPP_CHARGING_PROFILE_PURPOSE_TX;
	}else{
		ESP_LOGE(TAG, "Invalid purpose");
		return -1;
	}
}

enum ocpp_charging_profile_kind ocpp_charging_profile_kind_to_id(const char * profile_kind){
	if(strcmp(profile_kind, OCPP_CHARGING_PROFILE_KIND_ABSOLUTE) == 0){
		return eOCPP_CHARGING_PROFILE_KIND_ABSOLUTE;

	}else if(strcmp(profile_kind, OCPP_CHARGING_PROFILE_KIND_RECURRING) == 0){
		return eOCPP_CHARGING_PROFILE_KIND_RECURRING;

	}else if(strcmp(profile_kind, OCPP_CHARGING_PROFILE_KIND_RELATIVE) == 0){
		return eOCPP_CHARGING_PROFILE_KIND_RELATIVE;
	}else{
		ESP_LOGE(TAG, "Invalid profile kind");
		return -1;
	}
}

enum ocpp_recurrency_kind ocpp_recurrency_kind_to_id(const char * recurrency_kind){
	if(strcmp(recurrency_kind, OCPP_RECURRENCY_KIND_DAILY) == 0){
		return eOCPP_RECURRENCY_KIND_DAILY;

	}else if(strcmp(recurrency_kind, OCPP_RECURRENCY_KIND_WEEKLY) == 0){
		return eOCPP_RECURRENCY_KIND_WEEKLY;

	}else{
		ESP_LOGE(TAG, "Invalid recurrency kind");
		return -1;
	}
}

enum ocpp_charging_rate_unit ocpp_charging_rate_unit_to_id(const char * unit){
	if(strcmp(unit, OCPP_CHARGING_RATE_W) == 0){
		return eOCPP_CHARGING_RATE_W;

	}else if(strcmp(unit, OCPP_CHARGING_RATE_A) == 0){
		return eOCPP_CHARGING_RATE_A;

	}else{
		ESP_LOGE(TAG, "Invalid charging rate unit");
		return -1;
	}

}

const char * ocpp_charging_rate_unit_from_id(const enum ocpp_charging_rate_unit charge_rate_unit){
	switch(charge_rate_unit){
	case eOCPP_CHARGING_RATE_W:
		return OCPP_CHARGING_RATE_W;
	case eOCPP_CHARGING_RATE_A:
		return OCPP_CHARGING_RATE_A;
	default:
		ESP_LOGE(TAG, "Invalud conversion of charging rate unit");
		return "";
	}
}

static time_t default_start = 0;

static struct ocpp_charging_profile profile_default_max = {
	.profile_id = 0,
	.transaction_id = NULL,
	.stack_level = -1, // Lower priority than any valid profile
	.profile_purpose = eOCPP_CHARGING_PROFILE_PURPOSE_CHARGE_POINT_MAX,
	.profile_kind = eOCPP_CHARGING_PROFILE_KIND_ABSOLUTE,
	.recurrency_kind = NULL,
	.valid_from = 0,
	.valid_to = LONG_MAX,
	.charging_schedule.duration = NULL,
	.charging_schedule.start_schedule = &default_start,
	.charging_schedule.charge_rate_unit = eOCPP_CHARGING_RATE_A,
	.charging_schedule.min_charging_rate = 6.0f,
	.charging_schedule.schedule_period.value.start_period = 0,
	.charging_schedule.schedule_period.value.limit = 32.0f,
	.charging_schedule.schedule_period.value.number_phases = 3,
	.charging_schedule.schedule_period.next = NULL,
};

static struct ocpp_charging_profile profile_default_tx = {
	.profile_id = 0,
	.transaction_id = NULL,
	.stack_level = -1, // Lower priority than any valid profile
	.profile_purpose = eOCPP_CHARGING_PROFILE_PURPOSE_TX_DEFAULT,
	.profile_kind = eOCPP_CHARGING_PROFILE_KIND_ABSOLUTE,
	.recurrency_kind = NULL,
	.valid_from = 0,
	.valid_to = LONG_MAX,
	.charging_schedule.duration = NULL,
	.charging_schedule.start_schedule = &default_start,
	.charging_schedule.charge_rate_unit = eOCPP_CHARGING_RATE_A,
	.charging_schedule.min_charging_rate = 6.0f,
	.charging_schedule.schedule_period.value.start_period = 0,
	.charging_schedule.schedule_period.value.limit = 32.0f,
	.charging_schedule.schedule_period.value.number_phases = 3,
	.charging_schedule.schedule_period.next = NULL,
};

struct ocpp_charging_profile * ocpp_get_default_charging_profile(enum ocpp_charging_profile_purpose purpose){
	if(purpose == eOCPP_CHARGING_PROFILE_PURPOSE_CHARGE_POINT_MAX){
		return &profile_default_max;
	}else{
		return &profile_default_tx;
	}
}

static cJSON * ocpp_create_charging_schedule_period_list_json(struct ocpp_charging_schedule_period_list * schedule_period){

	cJSON * payload = cJSON_CreateArray();
	if(payload == NULL)
		return NULL;

	while(schedule_period != NULL){
		cJSON * period = cJSON_CreateObject();
		if(period == NULL)
			goto error;

		if(cJSON_AddNumberToObject(period, "startPeriod", schedule_period->value.start_period) == NULL)
			goto error;

		if(cJSON_AddNumberToObject(period, "limit", schedule_period->value.limit) == NULL)
			goto error;

		if(cJSON_AddNumberToObject(period, "numberPhases", schedule_period->value.number_phases) == NULL)
			goto error;

		cJSON_AddItemToArray(payload, period);

		schedule_period = schedule_period->next;
	}

	return payload;
error:
	cJSON_Delete(payload);
	return NULL;
}

cJSON * ocpp_create_charging_schedule_json(struct ocpp_charging_schedule * charging_schedule){
	cJSON * payload = cJSON_CreateObject();
	if(payload == NULL)
		return NULL;

	if(charging_schedule->duration != NULL && cJSON_AddNumberToObject(payload, "duration", *charging_schedule->duration) == NULL)
		goto error;

	if(charging_schedule->start_schedule != NULL){
		char timestamp_buffer[30];

		size_t written_length = ocpp_print_date_time(*charging_schedule->start_schedule,
							timestamp_buffer, sizeof(timestamp_buffer));
		if(written_length == 0)
			goto error;

		if(cJSON_AddStringToObject(payload, "startSchedule", timestamp_buffer) == NULL)
			goto error;
	}

	if(cJSON_AddStringToObject(payload, "chargingRateUnit", ocpp_charging_rate_unit_from_id(charging_schedule->charge_rate_unit)) == NULL)
		goto error;

	cJSON * period_list_json = ocpp_create_charging_schedule_period_list_json(&charging_schedule->schedule_period);
	if(period_list_json == NULL)
		goto error;

	cJSON_AddItemToObject(payload, "chargingSchedulePeriod", period_list_json);

	if(cJSON_AddNumberToObject(payload, "minChargingRate", charging_schedule->min_charging_rate) == NULL)
		goto error;

	return payload;
error:
	cJSON_Delete(payload);
	return NULL;
}

bool ocpp_period_is_equal_charge(const struct ocpp_charging_schedule_period * p1, const struct ocpp_charging_schedule_period * p2){

	if(p1 == p2)
		return true;

	if(p1 != NULL && p2 != NULL
		&& p1->limit == p2->limit
		&& p1->number_phases == p2->number_phases){

		return true;
	}

	return false;
}

bool allowed_units_converted = false;
char allowed_units_buffer[4];

const char * ocpp_get_allowed_charging_rate_units(){
	if(allowed_units_converted)
		return allowed_units_buffer;

	char tmp_allowed_config[16]; // configuration should have a maximum size equivalent to "Current,Power" (14)
	strncpy(tmp_allowed_config, CONFIG_OCPP_CHARGING_SCHEDULE_ALLOWED_CHARGING_RATE_UNIT, sizeof(tmp_allowed_config));

	char * buffer_end = allowed_units_buffer + (sizeof(allowed_units_buffer) -1);
	char * buffer_position = allowed_units_buffer;

	char * unit = strtok(tmp_allowed_config, ",");
	while(unit != NULL && buffer_position < buffer_end -1){ // While more units in configuration csl and space for ',' and value

		if(buffer_position != allowed_units_buffer) // Add ',' separator when not first/last value
			buffer_position = stpncpy(buffer_position, ",", buffer_end - buffer_position);

		if(strcmp(unit, "Current") == 0){
			buffer_position = stpncpy(buffer_position, "A", buffer_end - buffer_position);

		}else if(strcmp(unit, "Power") == 0){
			buffer_position = stpncpy(buffer_position, "W", buffer_end - buffer_position);
		}

		unit = strtok(NULL, ",");
	}

	ESP_LOGW(TAG, "Allowed units set as '%s'", allowed_units_buffer);
	allowed_units_converted = true;
	return allowed_units_buffer;
}
