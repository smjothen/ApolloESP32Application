#include <string.h>
#include "types/ocpp_phase_rotation.h"

enum ocpp_phase_rotation_id ocpp_phase_rotation_to_id(const char * phase_rotation){
	if(strcmp(phase_rotation, OCPP_PHASE_ROTATION_NOT_APPLICABLE) == 0){
		return eOCPP_PHASE_ROTATION_NOT_APPLICABLE;
	}else if(strcmp(phase_rotation, OCPP_PHASE_ROTATION_UNKNOWN) == 0){
		return eOCPP_PHASE_ROTATION_UNKNOWN;
	}else if(strcmp(phase_rotation, OCPP_PHASE_ROTATION_RST) == 0){
		return eOCPP_PHASE_ROTATION_RST;
	}else if(strcmp(phase_rotation, OCPP_PHASE_ROTATION_RTS) == 0){
		return eOCPP_PHASE_ROTATION_RTS;
	}else if(strcmp(phase_rotation, OCPP_PHASE_ROTATION_SRT) == 0){
		return eOCPP_PHASE_ROTATION_SRT;
	}else if(strcmp(phase_rotation, OCPP_PHASE_ROTATION_STR) == 0){
		return eOCPP_PHASE_ROTATION_STR;
	}else if(strcmp(phase_rotation, OCPP_PHASE_ROTATION_TRS) == 0){
		return eOCPP_PHASE_ROTATION_TRS;
	}else if(strcmp(phase_rotation, OCPP_PHASE_ROTATION_TSR) == 0){
		return eOCPP_PHASE_ROTATION_TSR;
	}else{
		return -1;
	}
}

const char * ocpp_phase_rotation_from_id(enum ocpp_phase_rotation_id id){

	switch(id){
	case eOCPP_PHASE_ROTATION_NOT_APPLICABLE:
		return OCPP_PHASE_ROTATION_NOT_APPLICABLE;
	case eOCPP_PHASE_ROTATION_UNKNOWN:
		return OCPP_PHASE_ROTATION_UNKNOWN;
	case eOCPP_PHASE_ROTATION_RST:
		return OCPP_PHASE_ROTATION_RST;
	case eOCPP_PHASE_ROTATION_RTS:
		return OCPP_PHASE_ROTATION_RTS;
	case eOCPP_PHASE_ROTATION_SRT:
		return OCPP_PHASE_ROTATION_SRT;
	case eOCPP_PHASE_ROTATION_STR:
		return OCPP_PHASE_ROTATION_STR;
	case eOCPP_PHASE_ROTATION_TRS:
		return OCPP_PHASE_ROTATION_TRS;
	case eOCPP_PHASE_ROTATION_TSR:
		return OCPP_PHASE_ROTATION_TSR;
	default:
		return NULL;
	}
}
