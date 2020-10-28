#ifndef _SESSIONHANDLER__H_
#define _SESSIONHANDLER__H_

#ifdef __cplusplus
extern "C" {
#endif


void sessionHandler_init();
void SetDataInterval(int newDataInterval);
int sessionHandler_GetStackWatermark();

#ifdef __cplusplus
}
#endif

#endif  /*_SESSIONHANDLER__H_*/
