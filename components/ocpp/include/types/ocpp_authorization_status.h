#ifndef OCPP_AUTHORIZATION_STATUS_H
#define OCPP_AUTHORIZATION_STATUS_H

/** @file
 * @brief Contains the OCPP type AuthorizationStatus
 */

/** @name AuthorizationStatus
 * @brief Status in a response to an Authorize.req.
 */
///@{
#define OCPP_AUTHORIZATION_STATUS_ACCEPTED "Accepted" ///< "Identifier is allowed for charging."
#define OCPP_AUTHORIZATION_STATUS_BLOCKED "Blocked" ///< "Identifier has been blocked. Not allowed for charging."
#define OCPP_AUTHORIZATION_STATUS_EXPIRED "Expired" ///< "Identifier has expired. Not allowed for charging."
#define OCPP_AUTHORIZATION_STATUS_INVALID "Invalid" ///< "Identifier is unknown. Not allowed for charging."
#define OCPP_AUTHORIZATION_STATUS_CONCURRENT_TX "ConcurrentTx" ///< "Identifier is already involved in another transaction and multiple transactions are not allowed. (Only relevant for a StartTransaction.req.)"
///@}
#endif /*OCPP_AUTHORIZATION_STATUS_H*/
