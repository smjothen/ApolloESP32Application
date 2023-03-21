#ifndef OCPP_UPDATE_STATUS_H
#define OCPP_UPDATE_STATUS_H

/** @file
* @brief Contains the OCPP type UpdateStatus
*/

/** @name
* @brief "Type of update for a SendLocalList.req."
*/
///@{
#define OCPP_UPDATE_STATUS_ACCEPTED "Accepted" ///< "Local Authorization List successfully updated."
#define OCPP_UPDATE_STATUS_FAILED "Failed"  ///< "Failed to update the Local Authorization List."
#define OCPP_UPDATE_STATUS_NOTSUPPORTED "NotSupported" ///< "Update of Local Authorization List is not supported by Charge Point."
#define OCPP_UPDATE_STATUS_VERSION_MISMATCH "VersionMismatch" ///< "Version number in the request for a differential update is less or equal then version number of current list."
///@}

/**
 * @brief Identifies the different authorization statuses.
 */
enum ocpp_update_status_id{
	eOCPP_UPDATE_STATUS_ACCEPTED,
	eOCPP_UPDATE_STATUS_FAILED,
	eOCPP_UPDATE_STATUS_NOTSUPPORTED,
	eOCPP_UPDATE_STATUS_VERSION_MISMATCH,
};

/**
 * @brief converts id to update status
 *
 * @param id update status id
 */
const char * ocpp_update_status_from_id(enum ocpp_update_status_id id);

/**
 * @brief converts update status to id
 *
 * @param status value to convert
 */
enum ocpp_update_status_id ocpp_update_status_to_id(const char * status);
#endif /*OCPP_UPDATE_STATUS_H*/
