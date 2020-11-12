#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "authentication.h"
#include "../i2c/include/CLRC661.h"
#include "string.h"
#include "../../main/storage.h"
#include "cJSON.h"

static const char *TAG = "AUTH     ";

#define MAX_NR_OF_TAGS 20

int nrOfUsedTags = 0;

void authentication_Init()
{
	//storage_ReadIDTags();
}


uint8_t authentication_CheckId(struct TagInfo tagInfo)
{
	uint8_t match = 0;

	storage_lookupRFIDTagInList(tagInfo.idAsString, &match);
	
	if(match == 1)
		ESP_LOGI(TAG, "Tag match in lookup table!");
	else
		ESP_LOGI(TAG, "No matching tag found");

	return match;
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
	return (MAX_NR_OF_TAGS - nrOfUsedTags);
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

		if(token_array_size > 20)
			token_array_size = 20;

		bool emptyList = false;

		if(token_array_size > 0)
		{
			cJSON *arrayItem = NULL;
			for (int i=0;i<token_array_size;i++) {
				arrayItem = cJSON_GetArrayItem(tokens,i);

				if(cJSON_HasObjectItem(arrayItem, "Tag"))
				{
					rfidTokens[i].Tag = cJSON_GetObjectItem(arrayItem,"Tag")->valuestring;

					//Check for clear message
					if((strcmp(rfidTokens[i].Tag, "*") == 0) && (token_array_size == 1))
					{
						esp_err_t err = storage_clearAllRFIDTagsOnFile();
						if(err == ESP_OK)
							ESP_LOGW(TAG, "Erase OK: %d ", err);
						else
							ESP_LOGE(TAG, "Erase ERROR: %d when erasing all tags", err);

						emptyList = true;

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

			if(!emptyList)
			{
				storage_printRFIDTagsOnFile();
				storage_updateRFIDTagsToFile(rfidTokens, token_array_size);
				storage_printRFIDTagsOnFile();
			}
		}

		//if(array != NULL)
			//cJSON_Delete(array);

		if(tokens != NULL)
			ESP_LOGI(TAG, "tokens: ");
			//cJSON_Delete(tokens);
	}

	if(tagPackage != NULL)
		cJSON_Delete(tagPackage);

	return version;
}