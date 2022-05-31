#ifndef OCPP_ID_TAG_INFO_H
#define OCPP_ID_TAG_INFO_H

struct ocpp_id_tag_info{
	time_t expiry_date;
	char parent_id_tag[21];
	char status[16];
};

#endif /*OCPP_ID_TAG_INFO_H*/
