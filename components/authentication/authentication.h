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
bool authentication_CheckId(struct TagInfo tagInfo);
void authentication_AddTag(struct TagItem tagItem);
void authentication_DeleteTag(struct TagItem tagItem);
int authentication_GetNrOfFreeTags();

#ifdef __cplusplus
}
#endif

#endif  /*_AUTHENTICATION_H_*/
