#ifndef _RFIDPAIRING_H_
#define _RFIDPAIRING_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "../i2c/include/CLRC661.h"


enum NFCPairingState
{
	ePairing_Inactive = 0,
	ePairing_Reading = 1,
	ePairing_Uploading = 2,
	ePairing_AddedOk = 3,
	ePairing_AddingFailed = 4
};

void rfidPairing_SetNewUserId(uint8_t * newUserId, uint16_t idLen);
void rfidPairing_SetNewTagId();//uint8_t * newName, uint16_t nameLen);
void rfidPairing_SetNewTagName(uint8_t * newTagId, uint16_t tagIdLen);
char * rfidPairing_GetConcatenatedString();
void rfidPairing_ClearBuffers();

enum NFCPairingState rfidPairing_GetState();
void rfidPairing_SetState(enum NFCPairingState state);

void rfidPairing_GetStateAsChar(char * stateAsChar);

bool rfidPairing_ClearState();


#ifdef __cplusplus
}
#endif

#endif  /*_RFIDPAIRING_H_*/
