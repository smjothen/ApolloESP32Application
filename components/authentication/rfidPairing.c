#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "string.h"

#include "rfidPairing.h"
#include "../i2c/include/CLRC661.h"
#include "../../main/storage.h"
#include "../audioBuzzer/audioBuzzer.h"
#include "../../components/i2c/include/i2cDevices.h"
#include "../zaptec_cloud/include/zaptec_cloud_observations.h"
#include "../../components/zaptec_protocol/include/protocol_task.h"

static const char *TAG = "RFID-PAIRING     ";

#define NFC_PAIR_TIMEOUT 20

enum NFCPairingState nfcPairingState = ePairing_Inactive;
enum NFCPairingState previousNfcPairingState = ePairing_Inactive;
uint32_t nfcPairTimeout = NFC_PAIR_TIMEOUT;


static char userIdBuf[40+1+1] = {0};
static char tagIdBuf[26] = {0};
static char tagNameBuf[33] = {0};
static char concatenatedString[100] = {0};

void rfidPairing_SetNewUserId(uint8_t * newUserId, uint16_t userIdLen)
{
	memset(userIdBuf, 0, 42);
	snprintf(userIdBuf, userIdLen+2, "%s;", newUserId);
	ESP_LOGI(TAG, "Auth UUID %s", userIdBuf);
}

void rfidPairing_SetNewTagId()
{
	memset(tagIdBuf, 0, 26);
	sprintf(tagIdBuf, "%s;", NFCGetTagInfo().idAsString);
	ESP_LOGI(TAG, "New RFID-UUID %s", tagIdBuf);
}

void rfidPairing_SetNewTagName(uint8_t * newName, uint16_t tagNameLen)
{
	memset(tagNameBuf, 0, 33);
	memcpy(tagNameBuf, newName, tagNameLen);
}

char * rfidPairing_GetConcatenatedString()
{
	memset(concatenatedString, 0, 100);
	strcpy(concatenatedString, userIdBuf);
	strcat(concatenatedString, &tagIdBuf[4]);
	strcat(concatenatedString, tagNameBuf);

	//Ensure buffer are cleared after use
	rfidPairing_ClearBuffers();

	ESP_LOGW(TAG, "Concatenated: %s", concatenatedString);

	return concatenatedString;
}

void rfidPairing_ClearBuffers()
{
	memset(userIdBuf, 0, 42);
	memset(tagIdBuf, 0, 26);
	memset(tagNameBuf, 0, 33);
}

enum NFCPairingState rfidPairing_GetState()
{
	return nfcPairingState;
}

void rfidPairing_SetState(enum NFCPairingState state)
{
	previousNfcPairingState = nfcPairingState;
	nfcPairingState = state;

	//Ensure NFC does not stay in pairing mode if bluetooth disconnects
	if(state == ePairing_Inactive)
		i2cSetNFCTagPairing(false);
}


void rfidPairing_GetStateAsChar(char * stateAsChar)
{
	if (nfcPairingState == ePairing_Inactive)
	{
		*stateAsChar = '0';
	}
	else if(nfcPairingState == ePairing_Reading)
	{


		*stateAsChar = '1';
		if(NFCGetTagInfo().tagIsValid == false)
		{
			i2cSetNFCTagPairing(true);
		}
		else
		{
			audio_play_single_biip();
			nfcPairingState = ePairing_Uploading;
		}
	}

	if(nfcPairingState == ePairing_Uploading)
	{
		if(NFCGetTagInfo().tagIsValid == true)
		{
			rfidPairing_SetNewTagId();
			char * combinedChargeCardString = rfidPairing_GetConcatenatedString();

			//Ensure we don't public empty strings
			if(*combinedChargeCardString !='\0')
				publish_debug_telemetry_observation_AddNewChargeCard(combinedChargeCardString);

			memset(concatenatedString, 0, 100);
			NFCClearTag();
		}

		*stateAsChar = '2';
	}
	else if(nfcPairingState == ePairing_AddedOk)
	{
		*stateAsChar = '3';
		if(previousNfcPairingState != nfcPairingState)
			audio_play_nfc_card_accepted();
	}
	else if(nfcPairingState == ePairing_AddingFailed)
	{
		*stateAsChar = '4';

		if(previousNfcPairingState != nfcPairingState)
			audio_play_nfc_card_denied();
	}

	if((nfcPairTimeout > 0) && (nfcPairingState == ePairing_Uploading))
	{
		nfcPairTimeout--;

		if(nfcPairTimeout == 0)
		{
			nfcPairingState = ePairing_AddingFailed;
			*stateAsChar = '4';
			nfcPairTimeout = NFC_PAIR_TIMEOUT;
			audio_play_nfc_card_denied();
			ESP_LOGE(TAG, "Pairing timed out");
		}
	}
	else if((nfcPairingState == ePairing_Reading) && (nfcPairTimeout > 0))
	{
		nfcPairTimeout--;
		if(nfcPairTimeout == 0)
		{
			nfcPairingState = ePairing_AddingFailed;
			MCU_StopLedOverride();
		}

	}
	else
	{
		nfcPairTimeout = NFC_PAIR_TIMEOUT;
	}


	ESP_LOGI(TAG, "Reading Pair NFC State: %s: timeout: %" PRId32 "", stateAsChar, nfcPairTimeout);
}


bool rfidPairing_ClearState()
{
	//Clear final states after BLE readout
	if((nfcPairingState == ePairing_AddedOk) || (nfcPairingState == ePairing_AddingFailed))
	{
		nfcPairingState = ePairing_Inactive;
		NFCClearTag();
		return true;
	}

	return false;
}



