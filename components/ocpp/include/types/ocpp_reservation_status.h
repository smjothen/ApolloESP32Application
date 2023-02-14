#ifndef OCPP_RESERVATION_STATUS_H
#define OCPP_RESERVATION_STATUS_H

/** @file
* @brief Contains the OCPP type ReservationStatus
*/

/** @name ReservationStatus
* @brief "Status in ReserveNow.conf."
*/
///@{
#define OCPP_RESERVATION_STATUS_ACCEPTED "Accepted" ///< "Reservation has been made."
#define OCPP_RESERVATION_STATUS_FAULTED "Faulted" ///< "Reservation has not been made, because connectors or specified connector are in a faulted state."
#define OCPP_RESERVATION_STATUS_OCCUPIED "Occupied" ///< "Reservation has not been made. All connectors or the specified connector are occupied."
#define OCPP_RESERVATION_STATUS_REJECTED "Rejected" ///< "Reservation has not been made. Charge Point is not configured to accept reservations."
#define OCPP_RESERVATION_STATUS_UNAVAILABLE "Unavailable" ///< "Reservation has not been made, because connectors or specified connector are in an unavailable state."
///@}

#endif /*OCPP_RESERVATION_STATUS_H*/
