#ifndef OCPP_CHARGE_POINT_ERROR_CODE_H
#define OCPP_CHARGE_POINT_ERROR_CODE_H

/** @file
* @brief Contains the OCPP type ChargePointErrorCode
*/

/** @name ChargePointErrorCode
* @brief Charge Point status reported in StatusNotification.req.
*/
///@{
#define OCPP_CP_ERROR_CONNECTOR_LOCK_FAILURE "ConnectorLockFailure" ///< "Failure to lock or unlock connector"
/**
 * @brief "Communication failure with the vehicle, might be Mode 3 or other communication protocol problem. This is not
 * a real error in the sense that the Charge Point doesn’t need to go to the faulted state. Instead, it should
 * go to the SuspendedEVSE state."
 */
#define OCPP_CP_ERROR_EV_COMMUNICATION_ERROR "EVCommunicationError"
#define OCPP_CP_ERROR_GROUND_FAILURE "GroundFailure" ///< "Ground fault circuit interrupter has been activated"
#define OCPP_CP_ERROR_HIGH_TEMPERATURE "HighTemperature" ///< "Temperature inside Charge Point is too high."
#define OCPP_CP_ERROR_INTERNAL_ERROR "InternalError" ///< "Error in internal hard- or software component."
#define OCPP_CP_ERROR_LOCAL_LIST_CONFLICT "LocalListConflict" ///< "The authorization information received from the Central System is in conflict with the LocalAuthorizationList"
#define OCPP_CP_ERROR_NO_ERROR "NoError" ///< "No error to report"
#define OCPP_CP_ERROR_OTHER_ERROR "OtherError" ///< "Other type of error. More information in vendorErrorCode"
#define OCPP_CP_ERROR_OVER_CURRENT_FAILURE "OverCurrentFailure" ///< "Over current protection device has tripped"
#define OCPP_CP_ERROR_OVER_VOLTAGE "OverVoltage" ///< "Voltage has risen above an acceptable level"
#define OCPP_CP_ERROR_POWER_METER_FAILURE "PowerMeterFailure" ///< "Failure to read electrical/energy/power meter"
#define OCPP_CP_ERROR_POWER_SWITCH_FAILURE "PowerSwitchFailure" ///< "Failure to control power switch"
#define OCPP_CP_ERROR_READER_FAILURE "ReaderFailure" ///< "Failure with idTag reader"
#define OCPP_CP_ERROR_RESET_FAILURE "ResetFailure" ///< "Unable to perform a reset"
#define OCPP_CP_ERROR_UNDER_VOLTAGE "UnderVoltage" ///< "Voltage has dropped below an acceptable level"
#define OCPP_CP_ERROR_WEAK_SIGNAL "WeakSignal" ///< "Wireless communication device reports a weak signal"
///@}


/** @name ChargePointVendorErrorCode
 * @brief Error codes to be used with OCPP_CP_ERROR_OTHER_ERROR and specified by zaptec
 */
///@{
#define OCPP_CP_VENDOR_ERROR_COMMUNICATION_MODE_PROHIBITED "ProhibitedByCommunicationMode"
///@}

#endif /*OCPP_CHARGE_POINT_ERROR_CODE_H*/
