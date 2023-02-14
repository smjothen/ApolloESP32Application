#ifndef OCPP_AVAILABILITY_STATUS_H
#define OCPP_AVAILABILITY_STATUS_H

/** @file
 * @brief Contains the OCPP type AvailabilityStatus
 */

/** @name AvailabilityStatus
 * @brief Status returned in response to ChangeAvailability.req.
 */
///@{
#define OCPP_AVAILABILITY_STATUS_ACCEPTED "Accepted" ///< "Request has been accepted and will be executed"
#define OCPP_AVAILABILITY_STATUS_REJECTED "Rejected" ///< "Request has not been accepted and will not be executed"
#define OCPP_AVAILABILITY_STATUS_SCHEDULED "Scheduled" ///< "Request has been accepted and will be executed when transaction(s) in progress have finished"
///@}
#endif /*OCPP_AVAILABILITY_STATUS_H*/
