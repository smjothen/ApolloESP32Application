Capability submodule
====================

This is the Zaptec Go/Apollo ESP application.

The application uses a [Espressif IoT Development Framework](https://github.com/espressif/esp-idf).
Please check [ESP-IDF docs](https://docs.espressif.com/projects/esp-idf/en/v5.1.1/esp32/index.html) for getting started instructions.

## Build instructions ##

The application can be built for development or production. Once build type has been selected, run `idf.py build`. Once built `idf.py flash` can be used to flash a charger or `python mark_build.py` can be used to create a version for OTA.

#### Development build #####

This build has no configuration restrictions. There are however a few options that could improve development experience. These options chan be found at the following menuconfig paths:

* (Top)->Zaptec->ZAPTEC_USE_ADVANCED_CONSOLE: Allows sending commands to the charger via console.
* (Top)->Zaptec->ZAPTEC_ENABLE_LOGGING: Enables logging via UART.
* (Top)->Zaptec->ZAPTEC_RUN_FACTORY_TESTS: Makes the charger attempt to connect to ZapProgram for production test. When enabled it will display additional options to affect test procedure.
* (Top)->Component config->Zaptec cloud->ZAPTEC_CLOUD_USE_DEVELOPMENT_URL: Changes the url the charger will connect to.

#### Example code ####
-------------------------------------------------------------------------------------------------

#include "../components/capabilities/capabilities.c"
#include "../components/capabilities/include/list.h" 

static char * capabilityString = NULL;

/**
 * Call for any interface to report capabilities
 */
char * GetCapabilityString()
{
	return capabilityString;
}

/**
 * Call one to generate capability string
 */
void MakeCapabilityString()
{
	/// Device Type
	enum DeviceType deviceType = DEVICETYPE_GO;

	/// Meter calibrated
	uint32_t calibrationId = 0;
	bool isCalibrated = MCU_GetMidStoredCalibrationId(&calibrationId);
	if((isCalibrated == true) && (calibrationId != 0))
		ESP_LOGW(TAG_MAIN, "MID Calibration ID: %lu", calibrationId);
	else
		isCalibrated = false;

	/// OCPP
	list_t * ocppList = list_create(false, NULL);
	enum CommunicationMode ocppMode = OCPPVERSION_V1_6;
	list_add(ocppList, (void*)&ocppMode, sizeof(void*));

	/// Communication Modes	
	list_t * comList = list_create(false, NULL);
	enum CommunicationMode LTEMode = COMMUNICATIONMODE_LTE;
	enum CommunicationMode wifiMode = COMMUNICATIONMODE_WI_FI;
	list_add(comList, (void*)&LTEMode, sizeof(void*));
	list_add(comList, (void*)&wifiMode, sizeof(void*));

	/// Make capabilites
	const struct Capabilities capabilities = {
		.device_type 			= &deviceType,
		.meter_calibrated 		= &isCalibrated,
		.ocpp_versions 			= ocppList,
		.communication_modes 	= comList 
	};

	cJSON * capaObject = cJSON_CreateCapabilities(&capabilities);

	/// Keep only this string permanently in memory. 
	capabilityString = cJSON_PrintUnformatted(capaObject);
 
	/// Frees cJSON object and sublists by calling list_release() on each list used
	cJSON_DeleteCapabilities(&capabilities);	

	ESP_LOGW(TAG_MAIN, "Capabilities: %s", GetCapabilityString());
}