#include <string.h>

#include "types/ocpp_availability_type.h"

/**
 * @brief converts availability_type to id
 *
 * @param  availability_type value to convert
 */
enum ocpp_availability_type_id ocpp_availability_type_to_id(const char * availability_type){
	if(strcmp(availability_type, OCPP_AVAILABILITY_TYPE_INOPERATIVE) == 0){
		return eOCPP_AVAILABILTY_TYPE_INOPERATIVE;
	}else if(strcmp(availability_type, OCPP_AVAILABILITY_TYPE_OPERATIVE) == 0){
		return eOCPP_AVAILABILITY_TYPE_OPERATIVE;
	}else{
		return -1;
	}
}

/**
 * @brief converts id to availability_type
 *
 * @param id availability_type id
 */
const char * ocpp_availability_type_from_id(enum ocpp_availability_type_id id){
	switch(id){
    case eOCPP_AVAILABILTY_TYPE_INOPERATIVE:
		return OCPP_AVAILABILITY_TYPE_INOPERATIVE;
	case eOCPP_AVAILABILITY_TYPE_OPERATIVE:
		return OCPP_AVAILABILITY_TYPE_OPERATIVE;
	default:
		return "";
	}
}
