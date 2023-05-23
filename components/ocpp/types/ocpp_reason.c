#include <string.h>

#include "types/ocpp_reason.h"

enum ocpp_reason_id ocpp_reason_to_id(const char * reason){
	if(strcmp(reason, OCPP_REASON_DE_AUTHORIZED) == 0){
		return eOCPP_REASON_DE_AUTHORIZED;
	}else if(strcmp(reason, OCPP_REASON_EMERGENCY_STOP) == 0){
		return eOCPP_REASON_EMERGENCY_STOP;
	}else if(strcmp(reason, OCPP_REASON_EV_DISCONNECT) == 0){
		return eOCPP_REASON_EV_DISCONNECT;
	}else if(strcmp(reason, OCPP_REASON_HARD_RESET) == 0){
		return eOCPP_REASON_HARD_RESET;
	}else if(strcmp(reason, OCPP_REASON_LOCAL) == 0){
		return eOCPP_REASON_LOCAL;
	}else if(strcmp(reason, OCPP_REASON_OTHER) == 0){
		return eOCPP_REASON_OTHER;
	}else if(strcmp(reason, OCPP_REASON_POWER_LOSS) == 0){
		return eOCPP_REASON_POWER_LOSS;
	}else if(strcmp(reason, OCPP_REASON_REBOOT) == 0){
		return eOCPP_REASON_REBOOT;
	}else if(strcmp(reason, OCPP_REASON_REMOTE) == 0){
		return eOCPP_REASON_REMOTE;
	}else if(strcmp(reason, OCPP_REASON_SOFT_RESET) == 0){
		return eOCPP_REASON_SOFT_RESET;
	}else if(strcmp(reason, OCPP_REASON_UNLOCK_COMMAND) == 0){
		return eOCPP_REASON_UNLOCK_COMMAND;
	}else{
		return -1;
	}
}
const char * ocpp_reason_from_id(enum ocpp_reason_id id){
	switch(id){
	case eOCPP_REASON_DE_AUTHORIZED:
		return OCPP_REASON_DE_AUTHORIZED;
	case eOCPP_REASON_EMERGENCY_STOP:
		return OCPP_REASON_EMERGENCY_STOP;
	case eOCPP_REASON_EV_DISCONNECT:
		return OCPP_REASON_EV_DISCONNECT;
	case eOCPP_REASON_HARD_RESET:
		return OCPP_REASON_HARD_RESET;
	case eOCPP_REASON_LOCAL:
		return OCPP_REASON_LOCAL;
	case eOCPP_REASON_OTHER:
		return OCPP_REASON_OTHER;
	case eOCPP_REASON_POWER_LOSS:
		return OCPP_REASON_POWER_LOSS;
	case eOCPP_REASON_REBOOT:
		return OCPP_REASON_REBOOT;
	case eOCPP_REASON_REMOTE:
		return OCPP_REASON_REMOTE;
	case eOCPP_REASON_SOFT_RESET:
		return OCPP_REASON_SOFT_RESET;
	case eOCPP_REASON_UNLOCK_COMMAND:
		return OCPP_REASON_UNLOCK_COMMAND;
	default:
		return "";
	}
}
