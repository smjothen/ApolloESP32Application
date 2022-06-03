#ifndef _OFFLINEHANDLER_H_
#define _OFFLINEHANDLER_H_

#ifdef __cplusplus
extern "C" {
#endif



enum PingReplyState
{
	PING_REPLY_ONLINE 	= 0,
	PING_REPLY_AWAITING_CMD = 1,
	PING_REPLY_CMD_RECEIVED = 2,
	PING_REPLY_OFFLINE		= 3,
};

void offlineHandler_CheckPingReply();
void offlineHandler_UpdatePingReplyState(enum PingReplyState state);
enum PingReplyState offlineHandler_GetPingReplyState();
bool offlineHandler_IsPingReplyOffline();

void offlineHandler_CheckForSimulateOffline();
void offlineHandler_SimulateOffline(int offlineTime);
void offlineHandler_CheckForOffline();
void offlineHandler_ClearOfflineCurrentSent();

bool offlineHandler_IsRequestingCurrentWhenOnline();
void offlineHandler_SetRequestingCurrentWhenOnline(bool value);


#ifdef __cplusplus
}
#endif

#endif  /*_OFFLINEHANDLER_H_*/
