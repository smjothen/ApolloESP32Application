#include <string.h>

#include "types/ocpp_charge_point_status.h"

const char * ocpp_cp_status_from_id(enum ocpp_cp_status_id id){
	switch(id){
	case eOCPP_CP_STATUS_AVAILABLE:
		return OCPP_CP_STATUS_AVAILABLE;
	case eOCPP_CP_STATUS_PREPARING:
		return OCPP_CP_STATUS_PREPARING;
	case eOCPP_CP_STATUS_CHARGING:
		return OCPP_CP_STATUS_CHARGING;
	case eOCPP_CP_STATUS_SUSPENDED_EV:
		return OCPP_CP_STATUS_SUSPENDED_EV;
	case eOCPP_CP_STATUS_SUSPENDED_EVSE:
		return OCPP_CP_STATUS_SUSPENDED_EVSE;
	case eOCPP_CP_STATUS_FINISHING:
		return OCPP_CP_STATUS_FINISHING;
	case eOCPP_CP_STATUS_RESERVED:
		return OCPP_CP_STATUS_RESERVED;
	case eOCPP_CP_STATUS_UNAVAILABLE:
		return OCPP_CP_STATUS_UNAVAILABLE;
	case eOCPP_CP_STATUS_FAULTED:
		return OCPP_CP_STATUS_FAULTED;
	default:
		return NULL;
	}
}

