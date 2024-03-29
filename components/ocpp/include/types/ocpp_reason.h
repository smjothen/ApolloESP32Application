#ifndef OCPP_REASON_H
#define OCPP_REASON_H

/** @file
* @brief Contains the OCPP type Reason
*/

/** @name Reason
* @brief "Reason for stopping a transaction in StopTransaction.req."
*/
///@{
#define OCPP_REASON_DE_AUTHORIZED "DeAuthorized" ///< "The transaction was stopped because of the authorization status in a StartTransaction.conf"
#define OCPP_REASON_EMERGENCY_STOP "EmergencyStop" ///< "Emergency stop button was used."
#define OCPP_REASON_EV_DISCONNECT "EVDisconnected" ///< "disconnecting of cable, vehicle moved away from inductive charge unit."
#define OCPP_REASON_HARD_RESET "HardReset" ///< "A hard reset command was received."
/**
 * @brief "Stopped locally on request of the user at the Charge Point. This is a regular termination of a transaction. Examples: presenting
 * an RFID tag, pressing a button to stop."
 */
#define OCPP_REASON_LOCAL "Local"
#define OCPP_REASON_OTHER "Other" ///< "Any other reason."
#define OCPP_REASON_POWER_LOSS "PowerLoss" ///< "Complete loss of power."
#define OCPP_REASON_REBOOT "Reboot" ///< "A locally initiated reset/reboot occurred. (for instance watchdog kicked in)"
/**
 * @brief "Stopped remotely on request of the user. This is a regular termination of a transaction. Examples: termination using a
 * smartphone app, exceeding a (non local) prepaid credit."
 */
#define OCPP_REASON_REMOTE "Remote"
#define OCPP_REASON_SOFT_RESET "SoftReset" ///< "A soft reset command was received."
#define OCPP_REASON_UNLOCK_COMMAND "UnlockCommand" ///< "Central System sent an Unlock Connector command."
///@}

/**
 * @brief Identifies the reason
 */
enum ocpp_reason_id{
	eOCPP_REASON_OTHER = 0,
	eOCPP_REASON_DE_AUTHORIZED,
	eOCPP_REASON_EMERGENCY_STOP,
	eOCPP_REASON_EV_DISCONNECT,
	eOCPP_REASON_HARD_RESET,
	eOCPP_REASON_LOCAL,
	eOCPP_REASON_POWER_LOSS,
	eOCPP_REASON_REBOOT,
	eOCPP_REASON_REMOTE,
	eOCPP_REASON_SOFT_RESET,
	eOCPP_REASON_UNLOCK_COMMAND,
};

/**
 * @brief converts reason to id
 *
 * @param  reason value to convert
 */
enum ocpp_reason_id ocpp_reason_to_id(const char * reason);

/**
 * @brief converts id to reason
 *
 * @param id reason id
 */
const char * ocpp_reason_from_id(enum ocpp_reason_id id);

#endif /*OCPP_REASON_H*/
