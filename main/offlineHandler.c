#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_system.h"
#include "offlineHandler.h"
#include "sessionHandler.h"
#include "string.h"
#include "chargeSession.h"
#include "../components/zaptec_protocol/include/zaptec_protocol_serialisation.h"
#include "../components/zaptec_protocol/include/protocol_task.h"
#include "zaptec_cloud_listener.h"
#include "zaptec_cloud_observations.h"
#include "storage.h"
#include "chargeController.h"

static const char *TAG = "OFFLINEHANDLER ";

#define PING_REPLY_TIMER_LIMIT_TO_OFFLINE 120//30 //Test param
#define PING_REPLY_CHECK_INCHARGE_PERIODE 180//60 //Test param
#define PING_REPLY_TIMER_INCREASE (PING_REPLY_CHECK_INCHARGE_PERIODE/2)


static enum PingReplyState pingReplyState = PING_REPLY_ONLINE;
static uint32_t pingReplyTimerToOffline = 0;
static uint32_t pingReplyTimerToOnline = 0;
static uint32_t incrementingToOnlineLimit = PING_REPLY_CHECK_INCHARGE_PERIODE;
void offlineHandler_CheckPingReply()
{
	/// Check if we are waiting for the Cloud-command
	if(pingReplyState == PING_REPLY_AWAITING_CMD)
	{
		pingReplyTimerToOffline++;

		/// No cloud command received - start offline charging and do InChargePing periodically
		if(pingReplyTimerToOffline == PING_REPLY_TIMER_LIMIT_TO_OFFLINE)//TODO: define timeout
		{
			offlineHandler_UpdatePingReplyState(PING_REPLY_OFFLINE);
		}
	}
	else
	{
		pingReplyTimerToOffline = 0;
	}

	/// Check if InCharge has recovered and we should go back to online mode
	if(pingReplyState == PING_REPLY_OFFLINE)
	{
		pingReplyTimerToOnline++;

		/// Repeatedly, with lower interval, send pulse to try and get InCharge response
		if(pingReplyTimerToOnline % incrementingToOnlineLimit == 0)
		{
			/// Don't allow longer check period than 1 hour - TODO evaluate
			if(incrementingToOnlineLimit <= 3600)
				incrementingToOnlineLimit += PING_REPLY_TIMER_INCREASE;

			ESP_LOGW(TAG, "Pinging InCharge! - New incrementingToOnlineLimit: %" PRId32 "", incrementingToOnlineLimit);

			pingReplyTimerToOnline = 0;

			update_mqtt_event_pattern(true);
			//publish_cloud_pulse();
			publish_debug_telemetry_observation_RequestNewStartChargingCommand();
			update_mqtt_event_pattern(false);
		}
	}
	else
	{
		pingReplyTimerToOnline = 0;
		incrementingToOnlineLimit = PING_REPLY_CHECK_INCHARGE_PERIODE;
	}
}

void offlineHandler_UpdatePingReplyState(enum PingReplyState newState)
{
	char buf[50] = {0};
	char *pbuf = buf;

	//Debug, for printing out the state change to console
	for (int i = 0; i < 2; i++)
	{
		if(pingReplyState == PING_REPLY_ONLINE)
			sprintf(pbuf, "PING_REPLY_ONLINE");
		else if(pingReplyState == PING_REPLY_AWAITING_CMD)
			sprintf(pbuf, "PING_REPLY_AWAITING_CMD");
		else if(pingReplyState == PING_REPLY_CMD_RECEIVED)
			sprintf(pbuf, "PING_REPLY_CMD_RECEIVED");
		else if(pingReplyState == PING_REPLY_OFFLINE)
			sprintf(pbuf, "PING_REPLY_OFFLINE");

		if(i==0)
		{
			sprintf(pbuf+strlen(buf), " -> ");
			int len = strlen(buf);
			pbuf= &buf[len];
			pingReplyState = newState;
		}
	}

	ESP_LOGW(TAG, "Updating PingReplyState: %s", buf);
}

enum PingReplyState offlineHandler_GetPingReplyState()
{
	return pingReplyState;
}


bool offlineHandler_IsPingReplyOffline()
{
	return (pingReplyState == PING_REPLY_OFFLINE);
}

static uint32_t simulateOfflineTimeout = 180;
static bool simulateOffline = false;
void offlineHandler_SimulateOffline(int offlineTime)
{
	simulateOffline = true;
	simulateOfflineTimeout = offlineTime;
}


void offlineHandler_CheckForSimulateOffline()
{
	//Allow simulating timelimited offline mode initated with cloud command
	if(simulateOffline == true)
	{
		//Override state
		//isOnline = false;
		MqttSetSimulatedOffline(true);

		simulateOfflineTimeout--;
		if(simulateOfflineTimeout == 0)
		{
			simulateOffline= false;
			MqttSetSimulatedOffline(false);
		}
	}
}


static bool requestCurrentWhenOnline = false;//Debug change to true of online/offline testing

bool offlineHandler_IsRequestingCurrentWhenOnline()
{
	return requestCurrentWhenOnline;
}

void offlineHandler_SetRequestingCurrentWhenOnline(bool state)
{
	requestCurrentWhenOnline = state;
}


/// The main function where offline current is granted if command from Cloud is not received within time
static bool offlineCurrentSent = false;
void offlineHandler_CheckForOffline()
{

	int activeSessionId = strlen(chargeSession_GetSessionId());
	uint8_t chargeOperatingMode = MCU_GetChargeOperatingMode();

	//Handle charge session started offline
	if((activeSessionId > 0) && (chargeOperatingMode == CHARGE_OPERATION_STATE_REQUESTING))//2 = Requesting, add definitions
	{
		//Wait until a valid tag is registered.
		if((storage_Get_AuthenticationRequired() == 1) && (chargeSession_Get().AuthenticationCode[0] == '\0'))
			return;

		requestCurrentWhenOnline = true;

		/// Sets LED Green, indicating waiting for start command from Cloud(or from this OfflineHandler)
		MessageType ret = MCU_SendCommandId(CommandAuthorizationGranted);
		if(ret == MsgCommandAck)
		{
			ESP_LOGI(TAG, "Offline MCU Granted command OK");

			float offlineCurrent = storage_Get_DefaultOfflineCurrent();
			float maxCurrent = storage_Get_CurrentInMaximum();

			/// Do not allow offline charging if offlineCurrent(Based on MaxCurrent) is below 6A.
			if((offlineCurrent >= 6.0) && (maxCurrent >= 6.0))
			{

				MessageType ret = MCU_SendFloatParameter(ParamChargeCurrentUserMax, offlineCurrent);
				if(ret == MsgWriteAck)
				{
					bool isSent = chargeController_SendStartCommandToMCU(eCHARGE_SOURCE_START_OFFLINE);
					//MessageType ret = MCU_SendCommandId(CommandStartCharging);
					if(isSent)
					{
						ESP_LOGI(TAG, "Offline MCU Start command OK: %fA", offlineCurrent);
					}
					else
					{
						ESP_LOGI(TAG, "Offline MCU Start command FAILED");
					}
				}
				else
				{
					ESP_LOGE(TAG, "Offline MCU Start command FAILED");
				}
			}
			else
			{
				ESP_LOGE(TAG, "To low offlineCurrent or maxCurrent to send start command");
			}

		}
		else
		{
			ESP_LOGI(TAG, "Offline MCU Granted command FAILED");
		}
	}

	/// Handle existing charge session that has gone offline
	/// Handle charge session started offline
	else if((activeSessionId > 0) && ((chargeOperatingMode == CHARGE_OPERATION_STATE_CHARGING) || (chargeOperatingMode == CHARGE_OPERATION_STATE_PAUSED)) && !offlineCurrentSent)//2 = Requesting, add definitions
	{
		float offlineCurrent = storage_Get_DefaultOfflineCurrent();
		float maxCurrent = storage_Get_CurrentInMaximum();

		ESP_LOGI(TAG, "Sending offline current to MCU %2.1f (MaxCurrent: %2.1f)", offlineCurrent, maxCurrent);

		requestCurrentWhenOnline = true;

		///When comming online, only check maxCurrent since offline current may not have been synced yet
		if(maxCurrent >= 6.0)
		{

			MessageType ret = MCU_SendFloatParameter(ParamChargeCurrentUserMax, offlineCurrent);
			if(ret == MsgWriteAck)
			{
				//MessageType ret = MCU_SendCommandId(CommandStartCharging);
				bool isSent = chargeController_SendStartCommandToMCU(eCHARGE_SOURCE_GONE_OFFLINE);
				if(isSent)
				{
					offlineCurrentSent = true;
					ESP_LOGW(TAG, "### Offline MCU Start command OK: %fA ###", offlineCurrent);
				}
				else
				{
					ESP_LOGE(TAG, "Offline MCU Start command FAILED");
				}
			}
			else
			{
				ESP_LOGE(TAG, "Offline MCU Start command FAILED");
			}
		}
		else
		{
			ESP_LOGE(TAG, "To low maxCurrent to send start command");
		}
	}
	else if(offlineCurrentSent == true)
	{
		ESP_LOGW(TAG, "Offline current mode. (SimulateOfflineTimeout: %" PRId32 ")", simulateOfflineTimeout);
	}
}

void offlineHandler_ClearOfflineCurrentSent()
{
	offlineCurrentSent = false;
}
