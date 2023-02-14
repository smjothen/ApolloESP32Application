#ifndef OCPP_DIAGNOSTICS_STATUS_H
#define OCPP_DIAGNOSTICS_STATUS_H

/** @file
* @brief Contains the OCPP type DiagnosticsStatus
*/

/** @name DiagnosticsStatus
* @brief Status in DiagnosticsStatusNotification.req
*/
///@{
#define OCPP_DIAGNOSTICS_STATUS_IDLE "Idle"
#define OCPP_DIAGNOSTICS_STATUS_UPLOADED "Uploaded"
#define OCPP_DIAGNOSTICS_STATUS_UPLOAD_FAILED "UploadFailed"
#define OCPP_DIAGNOSTICS_STATUS_UPLOADING "Uploading"
///@}

/**
 * @brief Identifies the diagnostics status
 */
enum ocpp_diagnostics_status{
	/**
	 * @brief "Charge Point is not performing diagnostics related tasks. Status Idle SHALL only be used as in a
	 * DiagnosticsStatusNotification.req that was triggered by a TriggerMessage.req"
	 */
	eOCPP_DIAGNOSTICS_STATUS_IDLE,
	eOCPP_DIAGNOSTICS_STATUS_UPLOADED, ///< "Diagnostics information has been uploaded"
	eOCPP_DIAGNOSTICS_STATUS_UPLOAD_FAILED, ///< "Uploading of diagnostics failed"
	eOCPP_DIAGNOSTICS_STATUS_UPLOADING ///< "File is being uploaded"
};

/**
 * @brief converts a diagnostics status to its id
 *
 * @param status the diagnostics status to convert
 */
const char * ocpp_diagnostics_status_from_id(enum ocpp_diagnostics_status status);
#endif /*OCPP_DIAGNOSTICS_STATUS_H*/
