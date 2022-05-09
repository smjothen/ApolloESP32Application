#include <time.h>

#include "cJSON.h"

#include "types/ocpp_meter_value.h"
#include "types/ocpp_enum.h"

static cJSON * create_sampled_value_json(struct ocpp_sampled_value sample){
	if(sample.value == NULL)
		return NULL;

	if(sample.context != NULL && ocpp_validate_enum(sample.context, 8,
							OCPP_READING_CONTEXT_INTERRUPT_BEGIN,
							OCPP_READING_CONTEXT_INTERRUPT_END,
							OCPP_READING_CONTEXT_OTHER,
							OCPP_READING_CONTEXT_SAMPLE_CLOCK,
							OCPP_READING_CONTEXT_SAMPLE_PERIODIC,
							OCPP_READING_CONTEXT_TRANSACTION_BEGIN,
							OCPP_READING_CONTEXT_TRANSACTION_END,
							OCPP_READING_CONTEXT_TRIGGER) != 0){
		return NULL;
	}

	if(sample.format != NULL && ocpp_validate_enum(sample.format, 2,
							OCPP_VALUE_FORMAT_RAW,
							OCPP_VALUE_FORMAT_SIGNED_DATA) != 0){
		return NULL;
	}

	if(sample.measurand != NULL && ocpp_validate_enum(sample.measurand, 22,
								OCPP_MEASURAND_CURRENT_EXPORT,
								OCPP_MEASURAND_CURRENT_IMPORT,
								OCPP_MEASURAND_CURRENT_OFFERED,
								OCPP_MEASURAND_ENERGY_ACTIVE_EXPORT_REGISTER,
								OCPP_MEASURAND_ENERGY_ACTIVE_IMPORT_REGISTER,
								OCPP_MEASURAND_ENERGY_REACTIVE_EXPORT_REGISTER,
								OCPP_MEASURAND_ENERGY_REACTIVE_IMPORT_REGISTER,
								OCPP_MEASURAND_ENERGY_ACTIVE_EXPORT_INTERVAL,
								OCPP_MEASURAND_ENERGY_ACTIVE_IMPORT_INTERVAL,
								OCPP_MEASURAND_ENERGY_REACTIVE_EXPORT_INTERVAL,
								OCPP_MEASURAND_ENERGY_REACTIVE_IMPORT_INTERVAL,
								OCPP_MEASURAND_FREQUENCY,
								OCPP_MEASURAND_POWER_ACTIVE_EXPORT,
								OCPP_MEASURAND_POWER_ACTIVE_IMPORT,
								OCPP_MEASURAND_POWER_FACTOR,
								OCPP_MEASURAND_POWER_OFFERED,
								OCPP_MEASURAND_POWER_REACTIVE_EXPORT,
								OCPP_MEASURAND_POWER_REACTIVE_IMPORT,
								OCPP_MEASURAND_RPM,
								OCPP_MEASURAND_SOC,
								OCPP_MEASURAND_TEMERATURE,
								OCPP_MEASURAND_VOLTAGE) != 0){
		return NULL;
	}

	if(sample.phase != NULL && ocpp_validate_enum(sample.phase, 10,
							OCPP_PHASE_L1,
							OCPP_PHASE_L2,
							OCPP_PHASE_L3,
							OCPP_PHASE_N,
							OCPP_PHASE_L1_N,
							OCPP_PHASE_L2_N,
							OCPP_PHASE_L3_N,
							OCPP_PHASE_L1_L2,
							OCPP_PHASE_L2_L3,
							OCPP_PHASE_L3_L1) != 0){
		return NULL;
	}

	if(sample.location != NULL && ocpp_validate_enum(sample.location, 5,
							OCPP_LOCATION_BODY,
							OCPP_LOCATION_CABLE,
							OCPP_LOCATION_EV,
							OCPP_LOCATION_INLET,
							OCPP_LOCATION_OUTLET) != 0){
		return NULL;
	}

	if(sample.unit != NULL && ocpp_validate_enum(sample.unit, 15,
							OCPP_UNIT_OF_MEASURE_WH,
							OCPP_UNIT_OF_MEASURE_KWH,
							OCPP_UNIT_OF_MEASURE_VARH,
							OCPP_UNIT_OF_MEASURE_KVARH,
							OCPP_UNIT_OF_MEASURE_W,
							OCPP_UNIT_OF_MEASURE_KW,
							OCPP_UNIT_OF_MEASURE_VA,
							OCPP_UNIT_OF_MEASURE_KVA,
							OCPP_UNIT_OF_MEASURE_KVAR,
							OCPP_UNIT_OF_MEASURE_A,
							OCPP_UNIT_OF_MEASURE_V,
							OCPP_UNIT_OF_MEASURE_CELSIUS,
							OCPP_UNIT_OF_MEASURE_FAHRENHEIT,
							OCPP_UNIT_OF_MEASURE_K,
							OCPP_UNIT_OF_MEASURE_PERCENT) != 0){
		return NULL;
	}

	cJSON * result = cJSON_CreateObject();
	if(result == NULL)
		return NULL;

	cJSON * value_json = cJSON_CreateString(sample.value);
	if(value_json == NULL){
		goto error;
	}
	cJSON_AddItemToObject(result, "value", value_json);

	if(sample.context != NULL){
		cJSON * context_json = cJSON_CreateString(sample.context);
		if(context_json == NULL){
			goto error;
		}
		cJSON_AddItemToObject(result, "context", context_json);
	}
	if(sample.format != NULL){
		cJSON * format_json = cJSON_CreateString(sample.format);
		if(format_json == NULL){
			goto error;
		}
		cJSON_AddItemToObject(result, "format", format_json);
	}
	if(sample.measurand != NULL){
		cJSON * measurand_json = cJSON_CreateString(sample.measurand);
		if(measurand_json == NULL){
			goto error;
		}
		cJSON_AddItemToObject(result, "measurand", measurand_json);
	}
	if(sample.phase != NULL){
		cJSON * phase_json = cJSON_CreateString(sample.phase);
		if(phase_json == NULL){
			goto error;
		}
		cJSON_AddItemToObject(result, "phase", phase_json);
	}
	if(sample.location != NULL){
		cJSON * location_json = cJSON_CreateString(sample.location);
		if(location_json == NULL){
			goto error;
		}
		cJSON_AddItemToObject(result, "location", location_json);
	}
	if(sample.unit != NULL){
		cJSON * unit_json = cJSON_CreateString(sample.unit);
		if(unit_json == NULL){
			goto error;
		}
		cJSON_AddItemToObject(result, "unit", unit_json);
	}

	return result;
error:
	cJSON_Delete(result);
	return NULL;
}

cJSON * create_meter_value_json(struct ocpp_meter_value meter_value){
	char timestamp[30];
	size_t written_length = strftime(timestamp, sizeof(timestamp), "%FT%T%Z", localtime(&meter_value.timestamp));
	if(written_length != 0)
		return NULL;

	cJSON * result = cJSON_CreateObject();
	if(result == NULL)
		return NULL;

	cJSON * timestamp_json = cJSON_CreateString(timestamp);
	if(timestamp_json == NULL){
		cJSON_Delete(result);
		return NULL;
	}
	cJSON_AddItemToObject(result, "timestamp", timestamp_json);

	cJSON * sampled_json = create_sampled_value_json(meter_value.sampled_value);
	if(sampled_json == NULL){
		cJSON_Delete(result);
		return NULL;
	}
	return result;
}
