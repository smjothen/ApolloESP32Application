#ifndef OCPP_CHARGE_POINT_STATUS_H
#define OCPP_CHARGE_POINT_STATUS_H

/** @file
* @brief Contains the OCPP type ChargePointStatus
*/

/** @name ChargePointStatus
* @brief Status reported in StatusNotification.req.
*
* A status can be reported for the Charge Point main controller (connectorId = 0) or for a specific connector.
* Status for the Charge Point main controller is a subset of the enumeration: Available, Unavailable or Faulted.
*
* States considered Operative are: Available, Preparing, Charging, SuspendedEVSE, SuspendedEV, Finishing, Reserved.
* States considered Inoperative are: Unavailable, Faulted.
*/
///@{
#define OCPP_CP_STATUS_AVAILABLE "Available"
#define OCPP_CP_STATUS_PREPARING "Preparing"
#define OCPP_CP_STATUS_CHARGING "Charging"
#define OCPP_CP_STATUS_SUSPENDED_EVSE "SuspendedEVSE"
#define OCPP_CP_STATUS_SUSPENDED_EV "SuspendedEV"
#define OCPP_CP_STATUS_FINISHING "Finishing"
#define OCPP_CP_STATUS_RESERVED "Reserved"
#define OCPP_CP_STATUS_UNAVAILABLE "Unavailable"
#define OCPP_CP_STATUS_FAULTED "Faulted"
///@}

/**
 * @brief Identifies the different ocpp charge point statuses
 */
enum ocpp_cp_status_id{
	eOCPP_CP_STATUS_AVAILABLE = 1, ///< "When a Connector becomes available for a new user (Operative)"
	/**
	 * When a Connector becomes no longer available for a new user but there is no ongoing Transaction (yet). Typically a Connector
	 * is in preparing state when a user presents a tag, inserts a cable or a vehicle occupies the parking bay (Operative)
	 */
	eOCPP_CP_STATUS_PREPARING,
	eOCPP_CP_STATUS_CHARGING, ///< "When the contactor of a Connector closes, allowing the vehicle to charge (Operative)"
	/**
	 * When the EV is connected to the EVSE but the EVSE is not offering energy to the EV, e.g. due to a smart charging restriction,
	 * local supply power constraints, or as the result of StartTransaction.conf indicating that charging is not allowed etc.(Operative)
	 */
	eOCPP_CP_STATUS_SUSPENDED_EV,
	eOCPP_CP_STATUS_SUSPENDED_EVSE, ///< "When the EV is connected to the EVSE and the EVSE is offering energy but the EV is not taking any energy (Operative)"
	/**
	 * When a Transaction has stopped at a Connector, but the Connector is not yet available for a new user, e.g. the cable has not
	 * been removed or the vehicle has not left the parking bay (Operative)
	 */
	eOCPP_CP_STATUS_FINISHING,
	eOCPP_CP_STATUS_RESERVED, ///< "When a Connector becomes reserved as a result of a Reserve Now command (Operative)"
	/**
	 * "When a Connector becomes unavailable as the result of a Change Availability command or an event upon which the Charge
	 * Point transitions to unavailable at its discretion. Upon receipt of a Change Availability command, the status MAY change
	 * immediately or the change MAY be scheduled. When scheduled, the Status Notification shall be send when the availability
	 * change becomes effective (Inoperative)"
	 */
	eOCPP_CP_STATUS_UNAVAILABLE,
	eOCPP_CP_STATUS_FAULTED, ///< "When a Charge Point or connector has reported an error and is not available for energy delivery (Inoperative)"
};

#endif /*OCPP_CHARGE_POINT_STATUS_H*/
