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
#endif /*OCPP_UPDATE_STATUS_H*/
