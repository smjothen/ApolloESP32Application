#ifndef OCPP_UPDATE_TYPE_H
#define OCPP_UPDATE_TYPE_H

/** @file
* @brief Contains the OCPP type UpdateType
*/

/** @name UpdateType
* @brief Type of update for a SendLocalList.req.
*/
///@{
#define OCPP_UPDATE_TYPE_DIFFERENTIAL "Differential" ///< "Indicates that the current Local Authorization List must be updated with the values in this message."
#define OCPP_UPDATE_TYPE_FULL "Full" ///< "Indicates that the current Local Authorization List must be replaced by the values in this message."
///@}

#endif /*OCPP_UPDATE_TYPE_H*/
