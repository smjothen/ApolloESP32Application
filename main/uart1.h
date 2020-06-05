#ifndef _MBUS_H_
#define _MBUS_H_

#ifdef __cplusplus
extern "C" {
#endif

bool bufferReady;
bool newData;
unsigned char * obisRawData;
int receivedLength;
int obisRawDataLength;
uint32_t measurementNo;


enum LastTransmitType
{
	eNone,
	eCyclic,
	eThreshold
};

void mbus_init();
bool mbus_CheckReception();
uint32_t mbus_GetMeasurementNo();
void mbus_ButtonStartProductionTest();

#ifdef __cplusplus
}
#endif

#endif  /*_MBUS_H_*/




