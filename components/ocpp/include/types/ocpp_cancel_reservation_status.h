#ifndef OCPP_CANCEL_RESERVATION_STATUS_H
#define OCPP_CANCEL_RESERVATION_STATUS_H

/** @file
 * @brief Contains the OCPP type CancelReservationStatus
 */

/** @name CancelReservationStatus
 * @brief Status in CancelReservation.conf.
 */
///@{
#define OCPP_CANCEL_RESERVATION_STATUS_ACCEPTED "Accepted" ///< "Reservation for the identifier has been cancelled."
#define OCPP_CANCEL_RESERVATION_STATUS_REJECTED "Rejected" ///< "Reservation could not be cancelled, because there is no reservation active for the identifier."
///@}

#endif /*OCPP_CANCEL_RESERVATION_STATUS_H*/
