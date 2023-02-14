#ifndef OCPP_DATA_TRANSFER_STATUS_H
#define OCPP_DATA_TRANSFER_STATUS_H

/** @file
* @brief Contains the OCPP type DataTransferStatus
*/

/** @name DataTransferStatus
* @brief Status in DataTransfer.conf.
*/
///@{
#define OCPP_DATA_TRANSFER_STATUS_ACCEPTED "Accepted" ///< "Message has been accepted and the contained request is accepted"
#define OCPP_DATA_TRANSFER_STATUS_REJECTED "Rejected" ///< "Message has been accepted but the contained request is rejected"
#define OCPP_DATA_TRANSFER_STATUS_UNKNOWN_MESSAGE_ID "UnknownMessageId" ///< "Message could not be interpreted due to unknown messageId string"
#define OCPP_DATA_TRANSFER_STATUS_UNKNOWN_VENDOR_ID "UnknownVendorId" ///< "Message could not be interpreted due to unknown vendorId string"
///@}
#endif /*OCPP_DATA_TRANSFER_STATUS_H*/
