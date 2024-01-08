#include <stdio.h>

#include "utz.h"
#include "cJSON.h"

// TODO: Pass a struct with all the information to create the message
//
int midocmf_create_fiscal_message(char *msg_buf, size_t msg_size,
		const char *serial, const char *app_version, const char *mid_version, time_t time, uint32_t mid_status, uint32_t energyWh) {
	cJSON *OCMFObject = cJSON_CreateObject();
	if (!OCMFObject) {
		return -1;
	}

	char dtbuf[64];
	udatetime_t dt;

	utz_unix_to_datetime(time, &dt);
	utz_datetime_format_iso_utc(dtbuf, sizeof (dtbuf), &dt);

	double energykWh = energyWh / 1000.0;

	cJSON_AddStringToObject(OCMFObject, "FV", "1.0"); //FormatVersion
	cJSON_AddStringToObject(OCMFObject, "GI", "Zaptec Go Plus"); //GatewayIdentification
	cJSON_AddStringToObject(OCMFObject, "GS", serial); //GatewaySerial
	cJSON_AddStringToObject(OCMFObject, "GV", app_version); //GatewayVersion
	cJSON_AddStringToObject(OCMFObject, "MF", mid_version); //GatewayVersion
	cJSON_AddStringToObject(OCMFObject, "PG", "F1"); //Pagination(class)

	cJSON *readerArray = cJSON_CreateArray();
	cJSON *readerObject = cJSON_CreateObject();

	cJSON_AddStringToObject(readerObject, "TM", dtbuf); //TimeAndSyncState
	cJSON_AddNumberToObject(readerObject, "RV", energykWh); //ReadingValue
	cJSON_AddStringToObject(readerObject, "RI", "1-0:1.8.0"); //ReadingIdentification(OBIS-code)
	cJSON_AddStringToObject(readerObject, "RU", "kWh"); //ReadingUnit
	cJSON_AddStringToObject(readerObject, "RT", "AC"); //ReadingCurrentType
	// TODO: Do we send entries with meter errors to the cloud?
	cJSON_AddStringToObject(readerObject, "ST", "G"); //MeterState

	cJSON_AddItemToArray(readerArray, readerObject);
	cJSON_AddItemToObject(OCMFObject, "RD", readerArray);

	char *buf = cJSON_PrintUnformatted(OCMFObject);
	snprintf(msg_buf, msg_size, "OCMF|%s", buf);

	cJSON_Delete(OCMFObject);
	free(buf);

	return 0;
}
