#include <string.h>

#include "types/ocpp_authorization_status.h"

enum ocpp_authorization_status_id ocpp_authorization_status_to_id(const char * status){
	if(strcmp(status, OCPP_AUTHORIZATION_STATUS_ACCEPTED) == 0){
		return eOCPP_AUTHORIZATION_STATUS_ACCEPTED;
	}else if(strcmp(status, OCPP_AUTHORIZATION_STATUS_BLOCKED) == 0){
		return eOCPP_AUTHORIZATION_STATUS_BLOCKED;
	}else if(strcmp(status, OCPP_AUTHORIZATION_STATUS_EXPIRED) == 0){
		return eOCPP_AUTHORIZATION_STATUS_EXPIRED;
	}else if(strcmp(status, OCPP_AUTHORIZATION_STATUS_INVALID) == 0){
		return eOCPP_AUTHORIZATION_STATUS_INVALID;
	}else if(strcmp(status, OCPP_AUTHORIZATION_STATUS_CONCURRENT_TX) == 0){
		return eOCPP_AUTHORIZATION_STATUS_CONCURRENT_TX;
	}else{
		return -1;
	}
}

const char * ocpp_authorization_status_from_id(enum ocpp_authorization_status_id id){
	switch(id){
	case eOCPP_AUTHORIZATION_STATUS_ACCEPTED:
		return OCPP_AUTHORIZATION_STATUS_ACCEPTED;
	case eOCPP_AUTHORIZATION_STATUS_BLOCKED:
		return OCPP_AUTHORIZATION_STATUS_BLOCKED;
	case eOCPP_AUTHORIZATION_STATUS_EXPIRED:
		return OCPP_AUTHORIZATION_STATUS_EXPIRED;
	case eOCPP_AUTHORIZATION_STATUS_INVALID:
		return OCPP_AUTHORIZATION_STATUS_INVALID;
	case eOCPP_AUTHORIZATION_STATUS_CONCURRENT_TX:
		return OCPP_AUTHORIZATION_STATUS_CONCURRENT_TX;
	default:
		return NULL;
	}
}

