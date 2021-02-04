#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"

#include "storage.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "i2cDevices.h"

#include "zaptec_protocol_serialisation.h"
#include "DeviceInfo.h"

static const char *TAG = "STORAGE:";

#define CONFIG_FILE "CONFIG_FILE"
nvs_handle configuration_handle;

// "wifi"
nvs_handle wifi_handle;

#define CS_RESET_FILE "CS_RESET_FILE"
nvs_handle session_reset_handle;

nvs_handle rfid_tag_handle;

esp_err_t err;

static struct Configuration configurationStruct;


void storage_Init()
{
	esp_err_t err = nvs_flash_init();

	//err = nvs_flash_erase();
	//err = nvs_flash_init();

	if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		// NVS partition was truncated and needs to be erased
		// Retry nvs_flash_init
		ESP_ERROR_CHECK(nvs_flash_erase());
		err = nvs_flash_init();
	}
	ESP_ERROR_CHECK( err );

	    //err = print_what_saved();
	    //if (err != ESP_OK)
	    	//printf("Error (%s) reading data from NVS!\n", esp_err_to_name(err));

#ifndef DO_LOG
    esp_log_level_set(TAG, ESP_LOG_INFO);
#endif

//	esp_err_t ret = nvs_flash_init();
//	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
//	  ESP_ERROR_CHECK(nvs_flash_erase());
//	  ret = nvs_flash_init();
//	}
//	ESP_ERROR_CHECK(ret);

}

void storage_Init_Configuration()
{
	configurationStruct.saveCounter = 1;

	// Cloud settings
	configurationStruct.authenticationRequired	= 1;
	configurationStruct.currentInMaximum 		= 6.0;
	configurationStruct.currentInMinimum		= 6.0;
	configurationStruct.maxPhases 				= 3;
	configurationStruct.defaultOfflinePhase		= 1;
	configurationStruct.defaultOfflineCurrent	= 6.0;
	configurationStruct.isEnabled				= 1;

	memset(configurationStruct.installationId, 0, sizeof(DEFAULT_STR_SIZE));
	memset(configurationStruct.routingId, 0, sizeof(DEFAULT_STR_SIZE));
	memset(configurationStruct.chargerName, 0, sizeof(DEFAULT_STR_SIZE));

	configurationStruct.transmitInterval 		= 60;
	configurationStruct.transmitChangeLevel 	= 1.0;
	configurationStruct.diagnosticsMode			= 0;

	//Local settings

	configurationStruct.communicationMode 		= eCONNECTION_WIFI;//eCONNECTION_NONE;//TODO set default
	configurationStruct.hmiBrightness 			= 0.2;	//0.0-0.1
	configurationStruct.permanentLock 			= 0;	//0/1

	configurationStruct.standalone 				= 1;	//0/1
	configurationStruct.standalonePhase 		= 1;	//Nr
	configurationStruct.standaloneCurrent		= 10;	//A
	configurationStruct.maxInstallationCurrentConfig = 0;


	configurationStruct.phaseRotation			= 1;
	configurationStruct.networkType				= 0;
}


struct Configuration storage_GetConfigurationParameers()
{
	return configurationStruct;
}


void storage_Set_AuthenticationRequired(uint8_t newValue)
{
	configurationStruct.authenticationRequired = newValue;
}

void storage_Set_CurrentInMaximum(float newValue)
{
	configurationStruct.currentInMaximum = newValue;
}

void storage_Set_CurrentInMinimum(float newValue)
{
	configurationStruct.currentInMinimum = newValue;
}

void storage_Set_MaxPhases(uint8_t newValue)
{
	configurationStruct.maxPhases = newValue;
}

void storage_Set_DefaultOfflinePhase(uint8_t newValue)
{
	configurationStruct.defaultOfflinePhase = newValue;
}

void storage_Set_DefaultOfflineCurrent(float newValue)
{
	configurationStruct.defaultOfflineCurrent = newValue;
}

void storage_Set_IsEnabled(uint8_t newValue)
{
	configurationStruct.isEnabled = newValue;
}

//Max string length 32 characters
void storage_Set_InstallationId(char * newValue)
{
	strcpy(configurationStruct.installationId, newValue);
}

//Max string length 32 characters
void storage_Set_RoutingId(char * newValue)
{
	strcpy(configurationStruct.routingId, newValue);
}

//Max string length 32 characters
void storage_Set_ChargerName(char * newValue)
{
	strcpy(configurationStruct.chargerName, newValue);
}

void storage_Set_DiagnosticsMode(uint32_t newValue)
{
	configurationStruct.diagnosticsMode = newValue;
}

void storage_Set_TransmitInterval(uint32_t newValue)
{
	configurationStruct.transmitInterval = newValue;
}

void storage_Set_TransmitChangeLevel(float newValue)
{
	configurationStruct.transmitChangeLevel = newValue;
}


//Local settings


void storage_Set_CommunicationMode(uint8_t newValue)
{
	configurationStruct.communicationMode = newValue;
}

void storage_Set_HmiBrightness(float newValue)
{
	configurationStruct.hmiBrightness = newValue;
}

void storage_Set_PermanentLock(uint8_t newValue)
{
	configurationStruct.permanentLock = newValue;
}

void storage_Set_Standalone(uint8_t newValue)
{
	configurationStruct.standalone = newValue;
}

void storage_Set_StandalonePhase(uint8_t newValue)
{
	configurationStruct.standalonePhase = newValue;
}

void storage_Set_StandaloneCurrent(float newValue)
{
	configurationStruct.standaloneCurrent = newValue;
}

void storage_Set_MaxInstallationCurrentConfig(float newValue)
{
	configurationStruct.maxInstallationCurrentConfig = newValue;
}


void storage_Set_PhaseRotation(uint8_t newValue)
{
	configurationStruct.phaseRotation = newValue;
}

void storage_Set_NetworkType(uint8_t newValue)
{
	configurationStruct.networkType = newValue;
}




//****************************************************

uint8_t storage_Get_AuthenticationRequired()
{
	return configurationStruct.authenticationRequired;
}

float storage_Get_CurrentInMaximum()
{
	return configurationStruct.currentInMaximum;
}

float storage_Get_CurrentInMinimum()
{
	return configurationStruct.currentInMinimum;
}

uint8_t storage_Get_MaxPhases()
{
	return configurationStruct.maxPhases;
}

uint8_t storage_Get_DefaultOfflinePhase()
{
	return configurationStruct.defaultOfflinePhase;
}

float storage_Get_DefaultOfflineCurrent()
{
	return configurationStruct.defaultOfflineCurrent;
}

uint8_t storage_Get_IsEnabled()
{
	return configurationStruct.isEnabled;
}

char * storage_Get_InstallationId()
{
	return configurationStruct.installationId;
}

char * storage_Get_RoutingId()
{
	return configurationStruct.routingId;
}

char * storage_Get_ChargerName()
{
	return configurationStruct.chargerName;
}

uint32_t storage_Get_DiagnosticsMode()
{
	return configurationStruct.diagnosticsMode;
}

uint32_t storage_Get_TransmitInterval()
{
	return configurationStruct.transmitInterval;
}

float storage_Get_TransmitChangeLevel()
{
	return configurationStruct.transmitChangeLevel;
}


//Local settings

uint8_t storage_Get_CommunicationMode()
{
	return configurationStruct.communicationMode;
}

float storage_Get_HmiBrightness()
{
	return configurationStruct.hmiBrightness;
}

uint8_t storage_Get_PermanentLock()
{
	return configurationStruct.permanentLock;
}

uint8_t storage_Get_Standalone()
{
	return configurationStruct.standalone;
}

uint8_t storage_Get_StandalonePhase()
{
	return configurationStruct.standalonePhase;
}

float storage_Get_StandaloneCurrent()
{
	return configurationStruct.standaloneCurrent;
}

float storage_Get_MaxInstallationCurrentConfig()
{
	return configurationStruct.maxInstallationCurrentConfig;
}

uint8_t storage_Get_PhaseRotation()
{
	return configurationStruct.phaseRotation;
}

uint8_t storage_Get_NetworkType()
{
	return configurationStruct.networkType;
}


esp_err_t nvs_set_zfloat(nvs_handle_t handle, const char* key, float inputValue)
{
	uint32_t floatToInt;
	memcpy(&floatToInt, &inputValue, 4);
	err = nvs_set_u32(handle, key, floatToInt);
	return err;
}

esp_err_t nvs_get_zfloat(nvs_handle_t handle, const char* key, float * outputValue)
{
	uint32_t intToFloat;
	err = nvs_get_u32(handle, key, &intToFloat);
	memcpy(outputValue, &intToFloat, 4);

	return err;
}

esp_err_t storage_SaveConfiguration()
{
	volatile esp_err_t err;

	err = nvs_open(CONFIG_FILE, NVS_READWRITE, &configuration_handle);

	configurationStruct.saveCounter++;
	err += nvs_set_u32(configuration_handle, "SaveCounter", configurationStruct.saveCounter);

	//Cloud settings
	err += nvs_set_u8(configuration_handle, "AuthRequired", configurationStruct.authenticationRequired);
	err += nvs_set_zfloat(configuration_handle, "CurrInMax", configurationStruct.currentInMaximum);
	err += nvs_set_zfloat(configuration_handle, "CurrInMin", configurationStruct.currentInMinimum);
	err += nvs_set_u8(configuration_handle, "MaxPhases", configurationStruct.maxPhases);
	err += nvs_set_u8(configuration_handle, "DefOfflinePh", configurationStruct.defaultOfflinePhase);
	err += nvs_set_zfloat(configuration_handle, "DefOfflineCurr", configurationStruct.defaultOfflineCurrent);
	err += nvs_set_u8(configuration_handle, "IsEnabled", configurationStruct.isEnabled);
	err += nvs_set_str(configuration_handle, "InstId", configurationStruct.installationId);
	err += nvs_set_str(configuration_handle, "RoutId", configurationStruct.routingId);
	err += nvs_set_str(configuration_handle, "ChargerName", configurationStruct.chargerName);
	err += nvs_set_u32(configuration_handle, "DiagMode", configurationStruct.diagnosticsMode);
	err += nvs_set_u32(configuration_handle, "TxInterval", configurationStruct.transmitInterval);
	err += nvs_set_zfloat(configuration_handle, "TxChangeLevel", configurationStruct.transmitChangeLevel);

	//Local settings
	err += nvs_set_u8(configuration_handle, "ComMode", configurationStruct.communicationMode);
	err += nvs_set_zfloat(configuration_handle, "HmiBrightness", configurationStruct.hmiBrightness);
	err += nvs_set_u8(configuration_handle, "PermanentLock", configurationStruct.permanentLock);
	err += nvs_set_u8(configuration_handle, "Stdalone", configurationStruct.standalone);
	err += nvs_set_u8(configuration_handle, "StdalonePhase", configurationStruct.standalonePhase);
	err += nvs_set_zfloat(configuration_handle, "StdaloneCurr", configurationStruct.standaloneCurrent);
	err += nvs_set_zfloat(configuration_handle, "maxInstCurrConf", configurationStruct.maxInstallationCurrentConfig);
	err += nvs_set_u8(configuration_handle, "PhaseRotation", configurationStruct.phaseRotation);
	err += nvs_set_u8(configuration_handle, "NetworkType", configurationStruct.networkType);

	err += nvs_commit(configuration_handle);
	nvs_close(configuration_handle);

	//memset(&configurationStruct, 0, sizeof(configurationStruct));

	return err;
}


esp_err_t storage_ReadConfiguration()
{
	esp_err_t err;
	err = nvs_open(CONFIG_FILE, NVS_READONLY, &configuration_handle);

	err += nvs_get_u32(configuration_handle, "SaveCounter", &configurationStruct.saveCounter);

	//Cloud settings
	err += nvs_get_u8(configuration_handle, "AuthRequired", &configurationStruct.authenticationRequired);
	err += nvs_get_zfloat(configuration_handle, "CurrInMax", &configurationStruct.currentInMaximum);
	err += nvs_get_zfloat(configuration_handle, "CurrInMin", &configurationStruct.currentInMinimum);
	err += nvs_get_u8(configuration_handle, "MaxPhases", &configurationStruct.maxPhases);
	err += nvs_get_u8(configuration_handle, "DefOfflinePh", &configurationStruct.defaultOfflinePhase);
	err += nvs_get_zfloat(configuration_handle, "DefOfflineCurr", &configurationStruct.defaultOfflineCurrent);
	err += nvs_get_u8(configuration_handle, "IsEnabled", &configurationStruct.isEnabled);
	size_t readSize;
	err += nvs_get_str(configuration_handle, "InstId", NULL, &readSize);
	err += nvs_get_str(configuration_handle, "InstId", configurationStruct.installationId, &readSize);
	err += nvs_get_str(configuration_handle, "RoutId", NULL, &readSize);
	err += nvs_get_str(configuration_handle, "RoutId", configurationStruct.routingId, &readSize);
	err += nvs_get_str(configuration_handle, "ChargerName", NULL, &readSize);
	err += nvs_get_str(configuration_handle, "ChargerName", configurationStruct.chargerName, &readSize);
	err += nvs_get_u32(configuration_handle, "DiagMode", &configurationStruct.diagnosticsMode);
	err += nvs_get_u32(configuration_handle, "TxInterval", &configurationStruct.transmitInterval);
	err += nvs_get_zfloat(configuration_handle, "TxChangeLevel", &configurationStruct.transmitChangeLevel);


	//Local settings
	err += nvs_get_u8(configuration_handle, "ComMode", &configurationStruct.communicationMode);
	err += nvs_get_zfloat(configuration_handle, "HmiBrightness", &configurationStruct.hmiBrightness);
	err += nvs_get_u8(configuration_handle, "PermanentLock", &configurationStruct.permanentLock);
	err += nvs_get_u8(configuration_handle, "Stdalone", &configurationStruct.standalone);
	err += nvs_get_u8(configuration_handle, "StdalonePhase", &configurationStruct.standalonePhase);
	err += nvs_get_zfloat(configuration_handle, "StdaloneCurr", &configurationStruct.standaloneCurrent);
	err += nvs_get_zfloat(configuration_handle, "maxInstCurrConf", &configurationStruct.maxInstallationCurrentConfig);
	err += nvs_get_u8(configuration_handle, "PhaseRotation", &configurationStruct.phaseRotation);
	err += nvs_get_u8(configuration_handle, "NetworkType", &configurationStruct.networkType);

	nvs_close(configuration_handle);

	return err;
}



esp_err_t storage_SaveSessionResetInfo(char * csId, char * csStartTime, uint32_t csUnixTime, float csEnergy, char * csAuthCode)
{
	volatile esp_err_t err;

	err = nvs_open(CS_RESET_FILE, NVS_READWRITE, &session_reset_handle);

	err += nvs_set_str(session_reset_handle, "csId", csId);
	err += nvs_set_str(session_reset_handle, "csStartTime", csStartTime);
	err += nvs_set_u32(session_reset_handle, "csUnixTime", csUnixTime);
	err += nvs_set_zfloat(session_reset_handle, "csEnergy", csEnergy);
	err += nvs_set_str(session_reset_handle, "csAuthCode", csAuthCode);

	err += nvs_commit(session_reset_handle);
	nvs_close(session_reset_handle);

	return err;
}


esp_err_t storage_ReadSessionResetInfo(char * csId, char * csStartTime, uint32_t csUnixTime, float csEnergy, char * csAuthCode)
{
	size_t readSize = 0;

	esp_err_t err;
	err = nvs_open(CS_RESET_FILE, NVS_READONLY, &session_reset_handle);

	err += nvs_get_str(session_reset_handle, "csId", NULL, &readSize);
	//Only continue to read if there is a session start stored
	if((readSize > 0) && (err == ESP_OK))
	{
		err += nvs_get_str(session_reset_handle, "csId", csId, &readSize);

		err += nvs_get_str(session_reset_handle, "csStartTime", NULL, &readSize);
		err += nvs_get_str(session_reset_handle, "csStartTime", csStartTime, &readSize);

		err += nvs_get_u32(session_reset_handle, "csUnixTime", &csUnixTime);
		err += nvs_get_zfloat(session_reset_handle, "csEnergy", &csEnergy);

		err += nvs_get_str(session_reset_handle, "csAuthCode", NULL, &readSize);
		err += nvs_get_str(session_reset_handle, "csAuthCode", csAuthCode, &readSize);
	}

	nvs_close(session_reset_handle);

	return err;
}



size_t storage_CheckSessionResetFile()
{
	size_t readSize = 0;

	err = nvs_open(CS_RESET_FILE, NVS_READONLY, &session_reset_handle);
	err += nvs_get_str(session_reset_handle, "csId", NULL, &readSize);
	nvs_close(session_reset_handle);

	return readSize;
}


esp_err_t storage_clearSessionResetInfo()
{
	err = nvs_open(CS_RESET_FILE, NVS_READWRITE, &session_reset_handle);
	err += nvs_erase_all(session_reset_handle);
	err += nvs_commit(session_reset_handle);
	nvs_close(session_reset_handle);

	return err;
}


esp_err_t storage_updateRFIDTagsToFile(volatile struct RFIDTokens rfidTokens[], uint32_t nrOfTokens)
{
	//Add(action = 0) to empty list or add to existing list up to MAX_NR_OF_RFID_TAGS.
	//Remove(action = 1) from list

	//Read existing nr of tags
	//If fewer than MAX_NR_OF_RFID_TAGS, then add.

	err = nvs_open("RFIDTags", NVS_READWRITE, &rfid_tag_handle);

	//Read nr of tags
	uint32_t nrOfTagsOnFile = 0;
	err += nvs_get_u32(rfid_tag_handle, "NrOfTagsSaved", &nrOfTagsOnFile);

	if(err != ESP_OK)
		nrOfTagsOnFile = 0;

	int removed = 0;
	int moved = 0;
	int actuallySaved = 0;

	//Only try to remove tags if there are any stored
	if(nrOfTagsOnFile > 0)
	{
		//First check for tags to remove. Doing this first is most space optimal.
		for (int tagNr = 0; tagNr < nrOfTokens; tagNr++)
		{
			//Check if this tag should be removed
			if(rfidTokens[tagNr].Action == 1)
			{
				size_t readSize;
				char keyErasePosName[15] = {0};
				sprintf(keyErasePosName, "TagNr%d", tagNr);

				//err += nvs_set_str(rfid_tag_handle, keyName, rfidTokens[tagNr].Tag);
				err += nvs_get_str(rfid_tag_handle, keyErasePosName, NULL, &readSize);

				//Don't allow excessive lengths
				if(readSize > 50)
					continue;

				char tagId[readSize];
				err += nvs_get_str(rfid_tag_handle, keyErasePosName, tagId, &readSize);

				//If match - remove
				if(strcmp(tagId, rfidTokens[tagNr].Tag) == 0)
				{
					err += nvs_erase_key(rfid_tag_handle, keyErasePosName);
					removed++;
					if(nrOfTagsOnFile >= 1)
						nrOfTagsOnFile--;
					ESP_LOGI(TAG, "Removed %s: %s, TagsOnFile: %d", keyErasePosName, rfidTokens[tagNr].Tag, nrOfTagsOnFile);

					//If there are more tags on file, move last item up to removed item.
					if((tagNr+1) < nrOfTagsOnFile)
					{
						//Read the key to move
						char keyToMoveName[15] = {0};
						sprintf(keyToMoveName, "TagNr%d", nrOfTagsOnFile);
						err += nvs_get_str(rfid_tag_handle, keyToMoveName, NULL, &readSize);
						err += nvs_get_str(rfid_tag_handle, keyToMoveName, tagId, &readSize);

						//Write the key to move
						err += nvs_set_str(rfid_tag_handle, keyErasePosName, tagId);

						//Erase the moved keys previous position
						err += nvs_erase_key(rfid_tag_handle, keyToMoveName);

						ESP_LOGI(TAG, "Moved %s -> %s", keyToMoveName, keyErasePosName);
						moved++;
					}
				}
			}
		}
	}

	int nrOfTagsOnFileInit = nrOfTagsOnFile;

	//Add tags to list if not already there
	for (int tagNr = 0; tagNr < nrOfTokens; tagNr++)
	{
		ESP_LOGI(TAG, "Action: %d",rfidTokens[tagNr].Action);

		//Check if this tag should be added
		if(rfidTokens[tagNr].Action == 0)
		{
			bool tagDuplicate = false;

			//Check if tag is already in saved list
			for (int tagOnFile = 0; tagOnFile < nrOfTagsOnFileInit; tagOnFile++)
			{
				size_t readSize;
				char keyName[15] = {0};
				sprintf(keyName, "TagNr%d", tagOnFile);

				err += nvs_get_str(rfid_tag_handle, keyName, NULL, &readSize);

				//Don't allow excessive lengths
				if(readSize > 50)
					continue;

				char tagId[readSize];
				err += nvs_get_str(rfid_tag_handle, keyName, tagId, &readSize);

				//If match - flag as duplicate
				if(strcmp(tagId, rfidTokens[tagNr].Tag) == 0)
				{
					ESP_LOGE(TAG, "%s duplicate!",keyName);
					tagDuplicate = true;
					break;
				}
			}

			//If we have found a duplicate, don't write, just continue with next tag
			if(tagDuplicate == true)
				continue;

			if(nrOfTagsOnFile < MAX_NR_OF_RFID_TAGS)
			{
				char keyName[15] = {0};
				sprintf(keyName, "TagNr%d", nrOfTagsOnFile);//Key name continuing from flash numbering
				err += nvs_set_str(rfid_tag_handle, keyName, rfidTokens[tagNr].Tag);
				actuallySaved++;
				nrOfTagsOnFile++;
			}
			else
			{
				//Error more than MAX_NR_OF_RFID_TAGS //TODO: Communicate to Cloud and app
				ESP_LOGE(TAG, "More tokens than allowed: %d", nrOfTagsOnFile);
			}
		}
	}

	//Update nr of NrOfTagsSaved
	err += nvs_set_u32(rfid_tag_handle, "NrOfTagsSaved", nrOfTagsOnFile);

	err = nvs_commit(rfid_tag_handle);
	nvs_close(rfid_tag_handle);

	if(err == ESP_OK)
		ESP_LOGI(TAG, "Removed: %d/%d, Saved: %d/%d, Moved: %d. On file: %d.", removed, nrOfTokens, actuallySaved, nrOfTokens, moved, nrOfTagsOnFile);
	else
		ESP_LOGI(TAG, "ERROR %d when saving %d new rfidTags",err, nrOfTokens);

	return err;
}



esp_err_t storage_lookupRFIDTagInList(char * tag, uint8_t *match)
{
	err = nvs_open("RFIDTags", NVS_READONLY, &rfid_tag_handle);

	//Read nr of tags
	uint32_t nrOfTagsOnFile = 0;
	err += nvs_get_u32(rfid_tag_handle, "NrOfTagsSaved", &nrOfTagsOnFile);

	if(err != ESP_OK)
		nrOfTagsOnFile = 0;

	//Only try to lookup if there are any stored
	if(nrOfTagsOnFile > 0)
	{
		//Look for tag
		for (uint8_t tagNr = 0; tagNr < nrOfTagsOnFile; tagNr++)
		{
			//Make key to search
			size_t readSize;
			char keyName[17] = {0};
			sprintf(keyName, "TagNr%d", tagNr);

			//Get tagId string length
			err += nvs_get_str(rfid_tag_handle, keyName, NULL, &readSize);

			//Don't allow excessive lengths
			if(readSize > 50)
				continue;

			//Read tagId string
			char tagId[readSize];
			err += nvs_get_str(rfid_tag_handle, keyName, tagId, &readSize);

			//If match set return value
			if(strcmp(tagId, tag) == 0)
			{
				*match = 1;
				ESP_LOGI(TAG, "Found match %s == %s, TagsOnFile: %d", keyName, tag, nrOfTagsOnFile);
			}

		}
		if(*match == 0)
		{
			ESP_LOGI(TAG, "No match with %d TagsOnFile",  nrOfTagsOnFile);
		}

	}
	else
	{
		ESP_LOGI(TAG, "Nothing to search, %d TagsOnFile",  nrOfTagsOnFile);
	}

	nvs_close(rfid_tag_handle);
	return err;
}


esp_err_t storage_printRFIDTagsOnFile()
{
	err = nvs_open("RFIDTags", NVS_READONLY, &rfid_tag_handle);

	//Read nr of tags
	uint32_t nrOfTagsOnFile = 0;
	err += nvs_get_u32(rfid_tag_handle, "NrOfTagsSaved", &nrOfTagsOnFile);

	if(err != ESP_OK)
		nrOfTagsOnFile = 0;

	//Only try to lookup if there are any stored
	if(nrOfTagsOnFile > 0)
	{
		//Look for tag
		for (uint8_t tagNr = 0; tagNr < nrOfTagsOnFile; tagNr++)
		{
			//Make key to search
			size_t readSize;
			char keyName[17] = {0};
			sprintf(keyName, "TagNr%d", tagNr);

			//Get tagId string length
			err += nvs_get_str(rfid_tag_handle, keyName, NULL, &readSize);

			//Don't allow excessive lengths
			if(readSize > 50)
				continue;

			//Read tagId string
			char tagId[readSize];
			err += nvs_get_str(rfid_tag_handle, keyName, tagId, &readSize);

			ESP_LOGI(TAG, "%d/%d: %s -> %s", tagNr+1, nrOfTagsOnFile, keyName, tagId);
		}
	}
	else
	{
		ESP_LOGI(TAG, "No tags on file: tagsOnFile: %d ", nrOfTagsOnFile);
	}

	nvs_close(rfid_tag_handle);
	return err;
}



esp_err_t storage_clearAllRFIDTagsOnFile()
{
	err = nvs_open("RFIDTags", NVS_READWRITE, &rfid_tag_handle);
	err += nvs_erase_all(rfid_tag_handle);
	err += nvs_commit(rfid_tag_handle);
	nvs_close(rfid_tag_handle);

	return err;
}



/*
 * Wifi parameters
 */
void storage_SaveWifiParameters(char *SSID, char *PSK)
{
	err = nvs_open("wifi", NVS_READWRITE, &wifi_handle);

	err += nvs_set_str(wifi_handle, "WifiSSID", SSID);
	err += nvs_set_str(wifi_handle, "WifiPSK", PSK);

	if(err == ESP_OK)
		ESP_LOGI(TAG, "Saved Wifi: SSID: %s PSK: %s successfully", SSID, PSK);
	else
		ESP_LOGI(TAG, "ERROR saving Wifi: SSID: %s PSK: %s", SSID, PSK);

	err = nvs_commit(wifi_handle);
	nvs_close(wifi_handle);

}


esp_err_t storage_ReadWifiParameters(char *SSID, char *PSK)
{
	struct DeviceInfo devInfo = i2cReadDeviceInfoFromEEPROM();
	if(devInfo.factory_stage != FactoryStageFinnished){
		ESP_LOGW(TAG, "Using factory SSID and PSK!!");
		// strcpy(SSID, "arntnett");
		// strcpy(PSK, "4703c87e817842c4ce6b167d43701b7685693846db");

		strcpy(SSID, "ZapWan");

		//Scramble to avoid readable string in bin-file
		int tmp = 13179704;
		tmp *= 4;
		sprintf(PSK,"%d", tmp);
		//strcpy(WifiPSK, "52718816");

		return 0;
	}

	size_t readSize;

	err = nvs_open("wifi", NVS_READONLY, &wifi_handle);

	err += nvs_get_str(wifi_handle, "WifiSSID", NULL, &readSize);
	err += nvs_get_str(wifi_handle, "WifiSSID", SSID, &readSize);

	err += nvs_get_str(wifi_handle, "WifiPSK", NULL, &readSize);
	err += nvs_get_str(wifi_handle, "WifiPSK", PSK, &readSize);

	ESP_LOGI(TAG, "Storage read Wifi: SSID: %s PSK: %s",SSID, PSK);

	nvs_close(wifi_handle);

	return err;
}


esp_err_t storage_clearWifiParameters()
{
	err = nvs_open("wifi", NVS_READWRITE, &wifi_handle);
	err += nvs_erase_all(wifi_handle);
	err += nvs_commit(wifi_handle);
	nvs_close(wifi_handle);

	return err;
}


void storage_GetStats()
{
	nvs_stats_t nvs_stats;
	nvs_get_stats(NULL, &nvs_stats);
	ESP_LOGI(TAG, "Count: UsedEntries = (%d), FreeEntries = (%d), AllEntries = (%d)\n", nvs_stats.used_entries, nvs_stats.free_entries, nvs_stats.total_entries);
}
