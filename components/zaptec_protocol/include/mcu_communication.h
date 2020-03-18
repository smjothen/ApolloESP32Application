#ifndef MCU_COMMUNICATION_H
#define MCU_COMMUNICATION_H
#include "zaptec_protocol_serialisation.h"
#include "freertos/FreeRTOS.h"

ZapMessage runRequest(const uint8_t *encodedTxBuf, uint length);

// MUST be called to relese lock for the ZapMessage reply from runRequest(),
// and to release the uart port for new request
void freeZapMessageReply(void);


#endif /* MCU_COMMUNICATION_H */
