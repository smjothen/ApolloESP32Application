#include <string.h>
#include "types/ocpp_update_status.h"

enum ocpp_update_status_id ocpp_update_status_to_id(const char * status){
	if(strcmp(status, OCPP_UPDATE_STATUS_ACCEPTED) == 0){
		return eOCPP_UPDATE_STATUS_ACCEPTED;
	}else if(strcmp(status, OCPP_UPDATE_STATUS_FAILED) == 0){
		return eOCPP_UPDATE_STATUS_FAILED;
	}else if(strcmp(status, OCPP_UPDATE_STATUS_NOTSUPPORTED) == 0){
		return eOCPP_UPDATE_STATUS_NOTSUPPORTED;
	}else if(strcmp(status, OCPP_UPDATE_STATUS_VERSION_MISMATCH) == 0){
		return eOCPP_UPDATE_STATUS_VERSION_MISMATCH;
	}else{
		return -1;
	}
}

const char * ocpp_update_status_from_id(enum ocpp_update_status_id id){
	switch(id){
	case eOCPP_UPDATE_STATUS_ACCEPTED:
		return OCPP_UPDATE_STATUS_ACCEPTED;
	case eOCPP_UPDATE_STATUS_FAILED:
		return OCPP_UPDATE_STATUS_FAILED;
	case eOCPP_UPDATE_STATUS_NOTSUPPORTED:
		return OCPP_UPDATE_STATUS_NOTSUPPORTED;
	case eOCPP_UPDATE_STATUS_VERSION_MISMATCH:
		return OCPP_UPDATE_STATUS_VERSION_MISMATCH;
	default:
		return NULL;
	}
}
