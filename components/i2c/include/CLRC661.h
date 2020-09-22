
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct TagInfo
{
	bool tagIsValid;
	uint8_t idLength;
	uint8_t id[10];
	char idAsString[21];
};

int NFCInit();
int NFCReadTag();
struct TagInfo NFCGetTagInfo();
void NFCClearTag();
void NFCTagInfoClearValid();

#ifdef __cplusplus
}
#endif
