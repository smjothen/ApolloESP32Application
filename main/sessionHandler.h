#ifndef _SESSIONHANDLER__H_
#define _SESSIONHANDLER__H_

#ifdef __cplusplus
extern "C" {
#endif


uint32_t GetTemplate();
void sessionHandler_init();
void SetDataInterval(int newDataInterval);

#ifdef __cplusplus
}
#endif

#endif  /*_SESSIONHANDLER__H_*/
