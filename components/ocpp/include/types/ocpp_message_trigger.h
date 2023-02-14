#ifndef OCPP_MESSAGE_TRIGGER_H
#define OCPP_MESSAGE_TRIGGER_H

/** @file
* @brief Contains the OCPP type MessageTrigger
*/

/** @name MessageTrigger
* @brief Type of request to be triggered in a TriggerMessage.req.
*/
///@{
#define OCPP_MESSAGE_TRIGGER_BOOT_NOTIFICATION "BootNotification" ///< "To trigger a BootNotification request"
#define OCPP_MESSAGE_TRIGGER_DIAGNOSTICS_STATUS_NOTIFICATION "DiagnosticsStatusNotification" ///< "To trigger a DiagnosticsStatusNotification request"
#define OCPP_MESSAGE_TRIGGER_FIRMWARE_STATUS_NOTIFICATION "FirmwareStatusNotification" ///< "To trigger a FirmwareStatusNotification request"
#define OCPP_MESSAGE_TRIGGER_HEARTBEAT "Heartbeat" ///< "To trigger a Heartbeat request"
#define OCPP_MESSAGE_TRIGGER_METER_VALUES "MeterValues" ///< "To trigger a MeterValues request"
#define OCPP_MESSAGE_TRIGGER_STATUS_NOTIFICATION "StatusNotification" ///< "To trigger a StatusNotification request"
///@}
#endif /*OCPP_MESSAGE_TRIGGER_H*/
