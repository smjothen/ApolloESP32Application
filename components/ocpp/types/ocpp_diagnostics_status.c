#include "types/ocpp_diagnostics_status.h"

const char * ocpp_diagnostics_status_from_id(enum ocpp_diagnostics_status status){
	switch(status){
	case eOCPP_DIAGNOSTICS_STATUS_IDLE:
		return OCPP_DIAGNOSTICS_STATUS_IDLE;
	case eOCPP_DIAGNOSTICS_STATUS_UPLOADED:
		return OCPP_DIAGNOSTICS_STATUS_UPLOADED;
	case eOCPP_DIAGNOSTICS_STATUS_UPLOAD_FAILED:
		return OCPP_DIAGNOSTICS_STATUS_UPLOAD_FAILED;
	case eOCPP_DIAGNOSTICS_STATUS_UPLOADING:
		return OCPP_DIAGNOSTICS_STATUS_UPLOADING;
	default:
		return "";
	}
}
