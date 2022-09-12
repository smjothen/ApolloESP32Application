#ifndef OCPP_ID_TAG_INFO_H
#define OCPP_ID_TAG_INFO_H

#include <time.h>

#include "cJSON.h"

#include "../ocpp_json/ocppj_message_structure.h"

struct ocpp_id_tag_info{
	time_t expiry_date;
	char parent_id_tag[21];
	char status[16];
};

ocppj_err_t id_tag_info_from_json(cJSON * idTagInfo, struct ocpp_id_tag_info * id_tag_out, char * error_description, size_t description_length);

#endif /*OCPP_ID_TAG_INFO_H*/
