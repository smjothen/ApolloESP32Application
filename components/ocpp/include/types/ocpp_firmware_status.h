#ifndef OCPP_FIRMWARE_STATUS_H
#define OCPP_FIRMWARE_STATUS_H

/** @file
* @brief Contains the OCPP type FirmwareStatus
*/

/** @name FirmwareStatus
* @brief Status of a firmware download as reported in FirmwareStatusNotification.req
*/
///@{
#define OCPP_FIRMWARE_STATUS_DOWNLOADED "Downloaded" ///< "New firmware has been downloaded by Charge Point"
#define OCPP_FIRMWARE_STATUS_DOWNLOAD_FAILED "DownloadFailed" ///< "Charge point failed to download firmware"
#define OCPP_FIRMWARE_STATUS_DOWNLOADING "Downloading" ///< "Firmware is being downloaded"
/**
 * @brief Charge Point is not performing firmware update related tasks. Status Idle SHALL only be used as in a
 * FirmwareStatusNotification.req that was triggered by a TriggerMessage.req
 */
#define OCPP_FIRMWARE_STATUS_IDLE "Idle"
#define OCPP_FIRMWARE_STATUS_INSTALLATION_FAILED "InstallationFailed" ///< "Installation of new firmware has failed"
#define OCPP_FIRMWARE_STATUS_INSTALLING "Installing" ///< "Firmware is being installed"
#define OCPP_FIRMWARE_STATUS_INSTALLED "Installed" ///< "New firmware has successfully been installed in charge point"
///@}

#endif /*OCPP_FIRMWARE_STATUS_H*/
