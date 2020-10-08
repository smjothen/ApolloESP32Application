#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "authentication.h"
#include "../i2c/include/CLRC661.h"
#include "string.h"


static const char *TAG = "AUTH     ";

#define MAX_NR_OF_TAGS 20


struct TagItem tagList[20] = {0};
int nrOfUsedTags = 0;

void authentication_Init()
{
	//storage_ReadIDTags();
}


bool authentication_CheckId(struct TagInfo tagInfo)
{
	tagList[0].indexUsed = true;
	uint32_t exTag = 0x3BAB3752;
	memcpy(tagList[0].tagId, &exTag, 4);
	//tagList[0].tagId = 0x5237AB3B;
//	tagList[0]->tagId[1] = 0x37;
//	tagList[0]->tagId[2] = 0xAB;
//	tagList[0]->tagId[3] = 0x3B;

	tagList[0].tagIdLength = 4;

	for (int i = 0; i < MAX_NR_OF_TAGS; i++)
	{
		if(tagList[i].indexUsed == true)

			if(memcmp(tagList[i].tagId ,&tagInfo.id, tagInfo.idLength)  == 0)
			{
				ESP_LOGI(TAG, "Id match!");
				return true;
			}

	}
	
	ESP_LOGI(TAG, "No matching NFC-tag found");

	return false;
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
