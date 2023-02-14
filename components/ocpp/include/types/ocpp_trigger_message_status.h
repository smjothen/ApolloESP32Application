#ifndef OCPP_TRIGGER_MESSAGE_STATUS_H
#define OCPP_TRIGGER_MESSAGE_STATUS_H

/** @file
* @brief Contains the OCPP type TriggerMessageStatus
*/

/** @name TriggerMessageStatus
* @brief Status in TriggerMessage.conf.
*/
///@{
#define OCPP_TRIGGER_MESSAGE_STATUS_ACCEPTED "Accepted" ///< "Requested notification will be sent."
#define OCPP_TRIGGER_MESSAGE_STATUS_REJECTED "Rejected" ///< "Requested notification will not be sent."
#define OCPP_TRIGGER_MESSAGE_STATUS_NOT_IMPLEMENTED "NotImplemented" ///< "Requested notification cannot be sent because it is either not implemented or unknown."
///@}

#endif /*OCPP_TRIGGER_MESSAGE_STATUS_H*/
