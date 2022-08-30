#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "authentication.h"
#include "../i2c/include/CLRC661.h"
#include "string.h"
#include "../../main/storage.h"
#include "../../main/chargeSession.h"
#include "../zaptec_protocol/include/protocol_task.h"
#include "cJSON.h"
#include "../../main/fat.h"
#include "../ocpp/include/types/ocpp_authorization_status.h"
static const char *TAG = "AUTH     ";

int nrOfUsedTags = 0;

static uint8_t isAuthenticationSet = 0;
static uint8_t previousIsAuthenticationSet = 0;

void authentication_Init()
{
	isAuthenticationSet = storage_Get_AuthenticationRequired();
	previousIsAuthenticationSet = isAuthenticationSet;
}


uint8_t authentication_CheckId(struct TagInfo tagInfo)
{
	uint8_t match = 0;

	struct ocpp_authorization_data auth_data = {0};
	if(storage_Get_ocpp_local_auth_list_enabled()){
		ESP_LOGI(TAG, "Attempting to match tag with local authorization list");
		if(!tagInfo.tagIsValid){
			ESP_LOGW(TAG, "Invalid tag, igoring local auth list");
		}else{
			if(fat_ReadAuthData(tagInfo.idAsString, &auth_data)){
				if(strcmp(auth_data.id_tag_info.status, OCPP_AUTHORIZATION_STATUS_ACCEPTED) == 0 &&
					(time(NULL) < auth_data.id_tag_info.expiry_date || auth_data.id_tag_info.expiry_date == 0)){
					ESP_LOGI(TAG, "Tag is accepted and not expired");
					return 1;
				}else{
					ESP_LOGI(TAG, "Tag is either expired or not accepted");
				}
			}else{
				ESP_LOGI(TAG, "Tag does not match any tag in local auth list");
			}
		}
	}

	storage_lookupRFIDTagInList(tagInfo.idAsString, &match);
	
	if(match == 1)
		ESP_LOGI(TAG, "Tag match in lookup table!");
	else
		ESP_LOGI(TAG, "No matching tag found");

	return match;
}

bool authentication_check_parent(const char * id_tag, const char * parent_id)
{
	if(storage_Get_ocpp_local_auth_list_enabled()){
		ESP_LOGI(TAG, "Attempting parent id tag comparison");

		struct ocpp_authorization_data auth_data = {0};
		if(!fat_ReadAuthData(id_tag, &auth_data)){
			return false;
		}

		if(strcasecmp(auth_data.id_tag_info.status, OCPP_AUTHORIZATION_STATUS_ACCEPTED) == 0
			&& strcmp(auth_data.id_tag_info.parent_id_tag, parent_id) == 0){

			return true;
		}
	}

	return false;
}

uint8_t authentication_CheckBLEId(char * bleUUID)
{
	uint8_t match = 0;

	storage_lookupRFIDTagInList(bleUUID, &match);

	if(match == 1)
	{
		ESP_LOGI(TAG, "BLE UUID match in lookup table!");
		authentication_Execute(bleUUID);
	}
	else
	{
		ESP_LOGI(TAG, "No matching BLE UUID found");
	}

	return match;
}


void authentication_Execute(char * authId)
{
	if(chargeSession_IsAuthenticated() == false)
	{
		///audio_play_nfc_card_accepted();
		ESP_LOGI(TAG, "Offline: NFC ACCEPTED - Local authentication");
		MessageType ret = MCU_SendCommandId(CommandAuthorizationGranted);
		if(ret == MsgCommandAck)
		{
			chargeSession_SetAuthenticationCode(authId);
			ESP_LOGI(TAG, "MCU: NFC ACCEPTED!");
		}
	}
}


void authentication_AddTag(struct TagItem tagItem)
{
	//Check for duplicates
}


void authentication_DeleteTag(struct TagItem tagItem)
{
	//Check for duplicates?
}

int authentication_GetNrOfFreeTags()
{
	return (MAX_NR_OF_RFID_TAGS - nrOfUsedTags);
}


int authentication_ParseOfflineList(char * message, int message_len)
{
	//strcpy(message,"[\"{\"Version\":1,\"Package\":0,\"PackageCount\":1,\"Type\":0,\"Tokens\":[{\"Tag\":\"*\",\"Action\":0,\"ExpiryDate\":null}]}\"]");
	//message_len = strlen(message);
	int version = 0;

	if((message[0] == '[') && (message[message_len-1] == ']'))
	{
		message += 2;
		message[message_len - 4] = '\0';
	}
	else
		return version;

	ESP_LOGI(TAG, "message=%s",message);

	cJSON *tagPackage = cJSON_Parse(message);//"{\"Version\":1,\"Package\":0,\"PackageCount\":1,\"Type\":0,\"Tokens\":[{\"Tag\":\"ble-f9f25dee-29c9-4eb2-af37-9f8e821ba0d9\",\"Action\":0,\"ExpiryDate\":null},{\"Tag\":\"ble-8b06fc14-aa7c-462d-a5d7-a7c943f2c4e0\",\"Action\":0,\"ExpiryDate\":null},{\"Tag\":\"nfc-5237AB3B\",\"Action\":0,\"ExpiryDate\":null},{\"Tag\":\"nfc-530796E7\",\"Action\":0,\"ExpiryDate\":null},{\"Tag\":\"nfc-034095E7\",\"Action\":0,\"ExpiryDate\":null},{\"Tag\":\"nfc-04C31102F84D80\",\"Action\":0,\"ExpiryDate\":null}]}");

	if(cJSON_HasObjectItem(tagPackage, "Version") && cJSON_HasObjectItem(tagPackage, "Package") && cJSON_HasObjectItem(tagPackage, "PackageCount") && cJSON_HasObjectItem(tagPackage, "Type"))
	{
		version = cJSON_GetObjectItem(tagPackage,"Version")->valueint;
		int package = cJSON_GetObjectItem(tagPackage,"Package")->valueint;
		int packageCount = cJSON_GetObjectItem(tagPackage,"PackageCount")->valueint;
		int type = cJSON_GetObjectItem(tagPackage,"Type")->valueint;

		ESP_LOGI(TAG, "version=%d",version);
		ESP_LOGI(TAG, "package=%d",package);
		ESP_LOGI(TAG, "packageCount=%d",packageCount);
		ESP_LOGI(TAG, "type=%d",type);

		cJSON *tokens = cJSON_GetObjectItem(tagPackage,"Tokens");
		//ESP_LOGI(TAG, "resolutions2->type=%s", JSON_Types(tokens->type));
		int token_array_size = cJSON_GetArraySize(tokens);

		struct RFIDTokens rfidTokens[token_array_size];
		memset(rfidTokens,0,sizeof(rfidTokens));

		ESP_LOGI(TAG, "token_array_size=%d", token_array_size);

		if(token_array_size > MAX_NR_OF_RFID_TAGS)
			token_array_size = MAX_NR_OF_RFID_TAGS;

		if(token_array_size > 0)
		{

			isAuthenticationSet = storage_Get_AuthenticationRequired();
			if((isAuthenticationSet == 1) && (previousIsAuthenticationSet == 0))
			{
				esp_err_t err = storage_clearAllRFIDTagsOnFile();
				if(err == ESP_OK)
					ESP_LOGW(TAG, "Setting Auth -> remove [*]. Erase OK: %d ", err);
				else
					ESP_LOGE(TAG, "Setting Auth -> remove [*]. Erase ERROR: %d when erasing all tags", err);
			}
			previousIsAuthenticationSet = isAuthenticationSet;


			cJSON *arrayItem = NULL;
			for (int i=0;i<token_array_size;i++) {
				arrayItem = cJSON_GetArrayItem(tokens,i);

				if(cJSON_HasObjectItem(arrayItem, "Tag"))
				{
					rfidTokens[i].Tag = cJSON_GetObjectItem(arrayItem,"Tag")->valuestring;

					//Check for clear message
					//if((strcmp(rfidTokens[i].Tag, "*") == 0) && (token_array_size == 1))

					//Ensure that if a single tag is added or removed, that the '*' tag is not cleared when "Disable authorization in offline" is set in portal"
					if((token_array_size == 1) && (type == 0))
					{
						esp_err_t err = storage_clearAllRFIDTagsOnFile();
						if(err == ESP_OK)
							ESP_LOGW(TAG, "Erase OK: %d ", err);
						else
							ESP_LOGE(TAG, "Erase ERROR: %d when erasing all tags", err);

						break;
					}

					if(cJSON_HasObjectItem(arrayItem, "Action"))
					{
						rfidTokens[i].Action = cJSON_GetObjectItem(arrayItem,"Action")->valueint;
						//Ignore expiry date used by OCPP for now

						ESP_LOGI(TAG, "rfidTokens[%d].Tag=%s",i, rfidTokens[i].Tag);
						ESP_LOGI(TAG, "rfidTokens[%d].Action=%d",i, rfidTokens[i].Action);

						//Ignore expiry date used by OCPP for now
						//cJSON_Delete(array);
					}
					ESP_LOGI(TAG, "");
				}
			}

			//if(!emptyList)
			//{
				storage_printRFIDTagsOnFile(false);
				storage_updateRFIDTagsToFile(rfidTokens, token_array_size);
				storage_printRFIDTagsOnFile(false);
			//}
		}

		//if(tokens != NULL)
			//ESP_LOGI(TAG, "tokens: ");

	}

	if(tagPackage != NULL)
		cJSON_Delete(tagPackage);

	return version;
}
