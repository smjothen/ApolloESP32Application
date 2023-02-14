#ifndef OCPP_CHARGING_PROFILE_STATUS_H
#define OCPP_CHARGING_PROFILE_STATUS_H

/** @file
* @brief Contains the OCPP type ChargingProfileStatus
*/

/** @name ChargingProfileStatus
* @brief Status returned in response to SetChargingProfile.req.
*/
///@{
#define OCPP_CHARGING_PROFILE_STATUS_ACCEPTED "Accepted" ///< "Request has been accepted and will be executed"
#define OCPP_CHARGING_PROFILE_STATUS_REJECTED "Rejected" ///< "Request has not been accepted and will not be executed"
#define OCPP_CHARGING_PROFILE_STATUS_NOT_SUPPORTED "NotSupported" ///< "Charge Point indicates that the request is not supported"
///@}
#endif /*OCPP_CHARGING_PROFILE_STATUS_H*/
