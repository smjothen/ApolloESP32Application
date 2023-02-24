#ifndef OCPP_AUTHORIZATION_DATA_H
#define OCPP_AUTHORIZATION_DATA_H

#include "ocpp_id_tag_info.h"

/** @file
 * @brief Contains the OCPP type AuthorizationData
 */

/**
 * @brief Elements that constitute an entry of a Local Authorization List update.
 */
struct ocpp_authorization_data{
	char id_tag[21]; ///< "Required. The identifier to which this authorization applies."
	/**
	 * @brief Optional. (Required when UpdateType is Full) This contains information about authorization status, expiry and parent id. For a Differential update the following
	 * applies: If this element is present, then this entry SHALL be added or updated in the Local Authorization List. If this element is absent, than the entry for this
	 * idtag in the Local Authorization List SHALL be deleted.
	 */
	struct ocpp_id_tag_info id_tag_info;
};

/**
 * @brief converts json to ocpp_authorization_data
 *
 * @param data The json payload to converts
 * @param authorization_data_out Output parameter containing result
 * @param error_description_out buffer to write to in case of error
 * @param error_description_length size of error_description_out buffer
 */
enum ocppj_err_t ocpp_authorization_data_from_json(cJSON * data, struct ocpp_authorization_data * authorization_data_out,
						char * error_description_out, size_t error_description_length);
#endif /*OCPP_AUTHORIZATION_DATA_H*/
