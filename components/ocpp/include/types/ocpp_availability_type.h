#ifndef OCPP_AVAILABILITY_TYPE_H
#define OCPP_AVAILABILITY_TYPE_H

/** @file
 * @brief Contains the OCPP type AvailabilityType
 */

/** @name AvailabilityType
 * @brief Requested availability change in ChangeAvailability.req.
 */
///@{
#define OCPP_AVAILABILITY_TYPE_INOPERATIVE "Inoperative" ///< "Charge point is not available for charging"
#define OCPP_AVAILABILITY_TYPE_OPERATIVE "Operative" ///< "Charge point is available for charging."
///@}

/**
 * @brief Identifies the availability type
 */
enum ocpp_availability_type_id{
	eOCPP_AVAILABILTY_TYPE_INOPERATIVE,
	eOCPP_AVAILABILITY_TYPE_OPERATIVE
};

/**
 * @brief converts availability_type to id
 *
 * @param  availability_type value to convert
 */
enum ocpp_availability_type_id ocpp_availability_type_to_id(const char * availability_type);

/**
 * @brief converts id to availability_type
 *
 * @param id availability_type id
 */
const char * ocpp_availability_type_from_id(enum ocpp_availability_type_id id);

#endif /*OCPP_AVAILABILITY_TYPE_H*/
