#include "messages/call_messages/ocpp_call_request.h"
#include "types/ocpp_ci_string_type.h"

cJSON * ocpp_create_boot_notification_request(const char * charge_box_serial_number, const char * charge_point_model,
							const char * charge_point_serial_number, const char * charge_point_vendor, const char * firmware_version,
						const char * iccid, const char * imsi, const char * meter_serial_number, const char * meter_type){

	if(charge_point_model == NULL || charge_point_vendor == NULL)
		return NULL;

	if(!is_ci_string_type(charge_point_model, 20) || !is_ci_string_type(charge_point_vendor, 20))
		return NULL;

	if((charge_box_serial_number != NULL && !is_ci_string_type(charge_box_serial_number, 25))
		|| (charge_point_serial_number != NULL &&  !is_ci_string_type(charge_point_serial_number, 25))
		|| (firmware_version != NULL && !is_ci_string_type(firmware_version, 50))
		|| (iccid != NULL && !is_ci_string_type(iccid, 20))
		|| (imsi != NULL && !is_ci_string_type(imsi, 20))
		|| (meter_serial_number != NULL && !is_ci_string_type(meter_serial_number, 25))
		|| (meter_type != NULL && !is_ci_string_type(meter_type, 25))
		)
		return NULL;

	cJSON * payload = cJSON_CreateObject();
	if(payload == NULL)
		return NULL;

	if(charge_box_serial_number != NULL){
		cJSON * charge_box_json = cJSON_CreateString(charge_box_serial_number);
		if(charge_box_json == NULL)
			goto error;

		cJSON_AddItemToObject(payload, "chargeBoxSerialNumber", charge_box_json);
	}

	cJSON * charge_point_model_json = cJSON_CreateString(charge_point_model);
	if(charge_point_model_json == NULL)
		goto error;

	cJSON_AddItemToObject(payload, "chargePointModel", charge_point_model_json);

	if(charge_point_serial_number != NULL){

		cJSON * charge_point_serial_number_json = cJSON_CreateString(charge_point_serial_number);
		if(charge_point_serial_number_json == NULL)
			goto error;

		cJSON_AddItemToObject(payload, "chargePointSerialNumber", charge_point_serial_number_json);
	}

	cJSON * charge_point_vendor_json = cJSON_CreateString(charge_point_vendor);
	if(charge_point_vendor_json == NULL)
		goto error;

	cJSON_AddItemToObject(payload, "chargePointVendor", charge_point_vendor_json);

	if(firmware_version != NULL){
		cJSON * firmware_version_json = cJSON_CreateString(firmware_version);
		if(firmware_version_json == NULL)
			goto error;

		cJSON_AddItemToObject(payload, "firmwareVersion", firmware_version_json);
	}

	if(iccid != NULL){
		cJSON * iccid_json = cJSON_CreateString(iccid);
		if(iccid_json == NULL)
			goto error;

		cJSON_AddItemToObject(payload, "iccid", iccid_json);
	}

	if(imsi != NULL){
		cJSON * imsi_json = cJSON_CreateString(imsi);
		if(imsi_json == NULL)
			goto error;

		cJSON_AddItemToObject(payload, "imsi", imsi_json);
	}

	if(meter_serial_number != NULL){
		cJSON * meter_serial_number_json = cJSON_CreateString(meter_serial_number);
		if(meter_serial_number_json == NULL)
			goto error;

		cJSON_AddItemToObject(payload, "meterSerialNumber", meter_serial_number_json);
	}

	if(meter_type != NULL){
		cJSON * meter_type_json = cJSON_CreateString(meter_type);
		if(meter_type_json == NULL)
			goto error;

		cJSON_AddItemToObject(payload, "meterType", meter_type_json);
	}

	cJSON * result = ocpp_create_call(OCPPJ_ACTION_BOOT_NOTIFICATION, payload);
	if(result == NULL){
		goto error;
	}
	else{
		return result;
	}

error:
	cJSON_Delete(payload);
	return NULL;
}
