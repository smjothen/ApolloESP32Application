#ifndef OCPP_CONFIGURATION_STATUS_H
#define OCPP_CONFIGURATION_STATUS_H

/** @file
* @brief Contains the OCPP type ConfigurationStatus
*/

/** @name ConfigurationStatus
* @brief Status in ChangeConfiguration.conf
*/
///@{
#define OCPP_CONFIGURATION_STATUS_ACCEPTED "Accepted" ///< "Configuration key is supported and setting has been changed"
#define OCPP_CONFIGURATION_STATUS_REJECTED "Rejected" ///< "Configuration key is supported, but setting could not be changed"
#define OCPP_CONFIGURATION_STATUS_REBOOT_REQUIRED "RebootRequired" ///< "Configuration key is supported and setting has been changed, but change will be available after reboot (Charge Point will not reboot itself)"
#define OCPP_CONFIGURATION_STATUS_NOT_SUPPORTED "NotSupported" ///< "Configuration key is not supported"
///@}
#endif /*OCPP_CONFIGURATION_STATUS_H*/
