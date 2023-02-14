#ifndef OCPP_RESET_TYPE_H
#define OCPP_RESET_TYPE_H

/** @file
* @brief Contains the OCPP type ResetType
*/

/** @name ResetType
* @brief Type of reset requested by Reset.req.
*/
///@{
/**
 * @brief "Restart (all) the hardware, the Charge Point is not required to gracefully stop ongoing transaction. If possible the Charge Point
 * sends a StopTransaction.req for previously ongoing transactions after having restarted and having been accepted by the
 * Central System via a BootNotification.conf. This is a last resort solution for a not correctly functioning Charge Point, by sending
 * a "hard" reset, (queued) information might get lost.""
 */
#define OCPP_RESET_TYPE_HARD "Hard"
/**
 * @brief "Stop ongoing transactions gracefully and sending StopTransaction.req for every ongoing transaction. It should then restart the
 * application software (if possible, otherwise restart the processor/controller)."
 */
#define OCPP_RESET_TYPE_SOFT "Soft"
///@}

#endif /*OCPP_RESET_TYPE_H*/
