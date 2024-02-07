#include <string.h>
#include "types/ocpp_registration_status.h"

const char * ocpp_registration_status_from_id(enum ocpp_registration_status id){
	switch(id){
	case eOCPP_REGISTRATION_ACCEPTED:
		return OCPP_REGISTRATION_ACCEPTED;
	case eOCPP_REGISTRATION_PENDING:
		return OCPP_REGISTRATION_PENDING;
	case eOCPP_REGISTRATION_REJECTED:
		return OCPP_REGISTRATION_REJECTED;
	default:
		return NULL;
	}
}
