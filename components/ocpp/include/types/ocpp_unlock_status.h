#ifndef OCPP_UNLOCK_STATUS_H
#define OCPP_UNLOCK_STATUS_H

/** @file
* @brief Contains the OCPP type UnlockStatus
*/

/** @name UnlockStatus
* @brief Status in response to UnlockConnector.req.
*/
///@{
#define OCPP_UNLOCK_STATUS_UNLOCKED "Unlocked" ///< "Connector has successfully been unlocked."
/**
 * @brief "Failed to unlock the connector: The Charge Point has tried to unlock the connector and has detected that the connector is still
 * locked or the unlock mechanism failed."
 */
#define OCPP_UNLOCK_STATUS_UNLOCK_FAILED "UnlockFailed"
#define OCPP_UNLOCK_STATUS_NOT_SUPPORTED "NotSupported" ///< "Charge Point has no connector lock, or ConnectorId is unknown."
///@}
#endif /*OCPP_UNLOCK_STATUS_H*/
