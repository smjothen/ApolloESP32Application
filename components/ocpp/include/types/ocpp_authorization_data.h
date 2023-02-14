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

#endif /*OCPP_AUTHORIZATION_DATA_H*/
