#ifndef _AUTHENTICATION_H_
#define _AUTHENTICATION_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "../i2c/include/CLRC661.h"


struct TagItem
{
	bool indexUsed;
	char tagName[20];
	uint8_t tagId[16];//RFID tags are max 10 bytes, but UUIDs are 16
	uint8_t tagIdLength;
};

void authentication_Init();
uint8_t authentication_CheckId(struct TagInfo tagInfo);
uint8_t authentication_CheckBLEId(char * bleUUID);
void authentication_Execute(char * authId);
void authentication_AddTag(struct TagItem tagItem);
void authentication_DeleteTag(struct TagItem tagItem);
int authentication_GetNrOfFreeTags();
int authentication_ParseOfflineList(char * message, int message_len);

#ifdef __cplusplus
}
#endif

#endif  /*_AUTHENTICATION_H_*/
