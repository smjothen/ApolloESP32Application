#ifndef OCPP_ID_TAG_INFO_H
#define OCPP_ID_TAG_INFO_H

#include <time.h>

#include "cJSON.h"

#include "../ocpp_json/ocppj_message_structure.h"
#include "ocpp_authorization_status.h"

/** @file
* @brief Contains the OCPP type IdTagInfo and helper functions
*/

/**
 * @brief Contains status information about an identifier. It is returned in Authorize, Start Transaction and Stop Transaction responses.
 *
 * If expiryDate is not given, the status has no end date.
 */
struct ocpp_id_tag_info{
	time_t expiry_date; ///< "Optional. This contains the date at which idTag should be removed from the Authorization Cache."
	char * parent_id_tag; ///< "Optional. This contains the parent-identifier."
	enum ocpp_authorization_status_id status; ///< "Required. This contains whether the idTag has been accepted or not by the Central System."
};

/**
 * @brief Convers tag info from json
 *
 * @param idTagInfo JSON data to parse
 * @param id_tag_out output parameter to create
 * @param error_description string to write error messages to
 * @param description_length maximum length of error description string
 */
enum ocppj_err_t id_tag_info_from_json(cJSON * idTagInfo, struct ocpp_id_tag_info * id_tag_out,
				char * error_description, size_t description_length);

/**
 * @brief Free allocated idTagInfo and its parent id
 *
 * @param id_tag_info idTagInfo to free
 */
void free_id_tag_info(struct ocpp_id_tag_info * id_tag_info);
#endif /*OCPP_ID_TAG_INFO_H*/
