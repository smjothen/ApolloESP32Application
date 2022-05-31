#ifndef OCPP_AUTHORIZATION_DATA_H
#define OCPP_AUTHORIZATION_DATA_H

#include "ocpp_id_tag_info.h"

struct ocpp_authorization_data{
	char id_tag[21];
	struct ocpp_id_tag_info id_tag_info;
};

#endif /*OCPP_AUTHORIZATION_DATA_H*/
