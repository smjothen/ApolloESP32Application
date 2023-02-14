#ifndef OCPP_REGISTRATION_STATUS_H
#define OCPP_REGISTRATION_STATUS_H

/** @file
* @brief Contains the OCPP type RegistrationStatus
*/

/** @name RegistrationStatus
* @brief "Result of registration in response to BootNotification.req."
*/
///@{
#define OCPP_REGISTRATION_ACCEPTED "Accepted"
#define OCPP_REGISTRATION_PENDING "Pending"
#define OCPP_REGISTRATION_REJECTED "Rejected"
///@}

/**
 * @brief Identifies the registration status
 */
enum ocpp_registration_status{
	eOCPP_REGISTRATION_ACCEPTED, ///< "Charge point is accepted by Central System."
	/**
	* @brief "Central System is not yet ready to accept the Charge Point. Central System may send messages to retrieve information or
	* prepare the Charge Point."
	*/
	eOCPP_REGISTRATION_PENDING,
	eOCPP_REGISTRATION_REJECTED ///< "Charge point is not accepted by Central System. This may happen when the Charge Point id is not known by Central System."
};

#endif /*OCPP_REGISTRATION_STATUS_H*/
