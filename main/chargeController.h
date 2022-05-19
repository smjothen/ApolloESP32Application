#ifndef _CHARGECONTROLLER_H_
#define _CHARGECONTROLLER_H_

#ifdef __cplusplus
extern "C" {
#endif


enum ChargeSource
{
	eCHARGE_SOURCE_STANDALONE 	= 1,
	eCHARGE_SOURCE_CLOUD 	  	= 2,
	eCHARGE_SOURCE_START_OFFLINE= 3,
	eCHARGE_SOURCE_GONE_OFFLINE = 4,
	eCHARGE_SOURCE_RAND_DELAY	= 5
};

static void RunStartChargeTimer();
static bool chargeController_SendStartCommandToMCU();

void chargeController_Init();
void chargeController_CancelDelay();
bool chargeController_SetStartCharging(enum ChargeSource source);
void chargeController_SetStartTimer();


#ifdef __cplusplus
}
#endif

#endif  /*_CHARGECONTROLLER_H_*/
