#ifndef OCPP_CHARGING_PROFILE_H
#define OCPP_CHARGING_PROFILE_H

#include <stdint.h>
#include <time.h>

#include "cJSON.h"

#include "ocpp_json/ocppj_message_structure.h"

/** @file
* @brief Contains the OCPP types related to ChargingProfile
*/

/** @name ChargingProfilePurposeType
* @brief Purpose of the charging profile.
*/
///@{
#define OCPP_CHARGING_PROFILE_PURPOSE_CHARGE_POINT_MAX "ChargePointMaxProfile"
#define	OCPP_CHARGING_PROFILE_PURPOSE_TX_DEFAULT "TxDefaultProfile"
#define	OCPP_CHARGING_PROFILE_PURPOSE_TX "TxProfile"
///@}

/**
 * @brief Identifies the different profile purposes
 */
enum ocpp_charging_profile_purpose{
	eOCPP_CHARGING_PROFILE_PURPOSE_CHARGE_POINT_MAX, ///< "Configuration for the maximum power or current available for an entire Charge Point"
	/**
	 * @brief "Default profile *that can be configured in the Charge Point. When a new transaction is started, this profile SHALL be used, unless it was a transaction that
	 * was started by a RemoteStartTransaction.req with a ChargeProfile that is accepted by the Charge Point."
	 */
	eOCPP_CHARGING_PROFILE_PURPOSE_TX_DEFAULT,
	/**
	 * @brief Profile with constraints to be imposed by the Charge Point on the current transaction, or on a new transaction when this is started via a
	 * RemoteStartTransaction.req with a ChargeProfile. A profile with this purpose SHALL cease to be valid when the transaction terminates.
	 */
	eOCPP_CHARGING_PROFILE_PURPOSE_TX,
};

/**
 * @brief converts a charging profile purpose to its id
 *
 * @param purpose charging profile purpose to convert
 */
enum ocpp_charging_profile_purpose ocpp_charging_profile_purpose_to_id(const char * purpose);

/** @name ChargingProfileKindType
* @brief Kind of charging profile.
*/
///@{
#define OCPP_CHARGING_PROFILE_KIND_ABSOLUTE "Absolute"
#define OCPP_CHARGING_PROFILE_KIND_RECURRING "Recurring"
#define OCPP_CHARGING_PROFILE_KIND_RELATIVE "Relative"
///@}

/**
 * @brief Identifies the profile kind
 */
enum ocpp_charging_profile_kind{
	eOCPP_CHARGING_PROFILE_KIND_ABSOLUTE, ///< "Schedule periods are relative to a fixed point in time defined in the schedule"
	eOCPP_CHARGING_PROFILE_KIND_RECURRING, ///< "The schedule restarts periodically at the first schedule period"
	eOCPP_CHARGING_PROFILE_KIND_RELATIVE ///< "Schedule periods are relative to a situation-specific start point (such as the start of a Transaction) that is determined by the charge point."
};

/**
 * @brief converts a charging profile kind to its id
 *
 * @param profile_kind charging profile kind to convert
 */
enum ocpp_charging_profile_kind ocpp_charging_profile_kind_to_id(const char * profile_kind);

/** @name RecurrencyKindType
* @brief Type of recurrence of a charging profile.
*/
///@{
#define OCPP_RECURRENCY_KIND_DAILY "Daily"
#define OCPP_RECURRENCY_KIND_WEEKLY "Weekly"
///@}

/**
 * @brief Identifies the recurrency kind
 */
enum ocpp_recurrency_kind{
	eOCPP_RECURRENCY_KIND_DAILY, ///< "The schedule restarts every 24 hours, at the same time as in the startSchedule"
	eOCPP_RECURRENCY_KIND_WEEKLY ///< "The schedule restarts every 7 days, at the same time and day-of-the-week as in the startSchedule"
};

/**
 * @brief converts a charging profile recurrency kind to its id
 *
 * @param recurrency_kind charging profile recurrency kind to convert
 */
enum ocpp_recurrency_kind ocpp_recurrency_kind_to_id(const char * recurrency_kind);

/** @name ChargingRateUnitType
* @brief Unit in which a charging schedule is defined, as used in: GetCompositeSchedule.req and ChargingSchedule
*/
///@{
#define OCPP_CHARGING_RATE_W "W"
#define OCPP_CHARGING_RATE_A "A"
///@}

/**
 * @brief Identifies the charging rate unit
 */
enum ocpp_charging_rate_unit{

	/**
	 * @brief "Watts (power)"
	 *
	 * "This is the TOTAL allowed charging power.
	 * If used for AC Charging, the phase current should be calculated via: Current per phase = Power / (Line Voltage * Number of
	 * Phases). The "Line Voltage" used in the calculation is not the measured voltage, but the set voltage for the area (hence, 230 of
	 * 110 volt). The "Number of Phases" is the numberPhases from the ChargingSchedulePeriod."
	 *
	 * "It is usually more convenient to use this for DC charging"
	 * "Note that if numberPhases in a ChargingSchedulePeriod is absent, 3 SHALL be assumed"
	 */
	eOCPP_CHARGING_RATE_W,
	/**
	 * @brief "Amperes (current)"
	 *
	 * "The amount of Ampere per phase, not the sum of all phases."
	 * "It is usually more convenient to use this for AC charging."
	 */
	eOCPP_CHARGING_RATE_A
};

/**
 * @brief converts a charging rate unit to its id
 *
 * @param unit charging rate unit to convert
 */
enum ocpp_charging_rate_unit ocpp_charging_rate_unit_to_id(const char * unit);

/**
 * @brief converts a charging rate unit id to its string value
 *
 * @param charge_rate_unit id to convert
 */
const char * ocpp_charging_rate_unit_from_id(const enum ocpp_charging_rate_unit charge_rate_unit);

/**
 * @brief "Charging schedule period structure defines a time period in a charging schedule"
 */
struct ocpp_charging_schedule_period{
	/**
	 * @brief "Required. Start of the period, in seconds from the start of schedule. The value of
	 * StartPeriod also defines the stop time of the previous period"
	 */
	int start_period;
	/**
	 * @brief "Required. Charging rate limit during the schedule period, in the applicable chargingRateUnit,
	 * for example in Amperes or Watts. Accepts at most one digit fraction (e.g. 8.1)."
	 */
	float limit;
	/**
	 * @brief "Optional. The number of phases that can be used for charging. If a number of
	 * phases is needed, numberPhases=3 will be assumed unless another number is given."
	 */
	int number_phases;
};

/**
 * @brief List of charging schedule periods
 */
struct ocpp_charging_schedule_period_list{
	struct ocpp_charging_schedule_period value; ///< The schedule period
	struct ocpp_charging_schedule_period_list * next; ///< The next item in the list or NULL if it is the last
};

/**
 * @brief "Charging schedule structure defines a list of charging periods"
 */
struct ocpp_charging_schedule{
	/**
	 * @brief "Optional. Duration of the charging schedule in seconds. If the duration is left empty,
	 * the last period will continue indefinitely or until end of the transaction in case startSchedule is absent."
	 */
	int * duration;
	time_t * start_schedule; ///< "Optional. Starting point of an absolute schedule. If absent the schedule will be relative to start of charging"
	enum ocpp_charging_rate_unit charge_rate_unit; ///< "Required. The unit of measure Limit is expressed in"
	/**
	 * @brief "Required. List of ChargingSchedulePeriod elements defining maximum power or current usage over time. The startSchedule of
	 * the first ChargingSchedulePeriod SHALL always be 0.
	 */
	struct ocpp_charging_schedule_period_list schedule_period;
	/**
	 * @brief "Optional. Minimum charging rate supported by the electric vehicle. The unit of measure is defined by the chargingRateUnit.
	 * This parameter is intended to be used by a local smart charging algorithm to optimize the power allocation for in the case a
	 * charging process is inefficient at lower charging rates. Accepts at most one digit fraction (e.g. 8.1)"
	 */
	float min_charging_rate;
};

/**
 * @brief A ChargingProfile consists of a ChargingSchedule, describing the amount of power or current that can be delivered per time interval.
 */
struct ocpp_charging_profile{
	int profile_id; ///< "Required. Unique identifier for this profile"
	int * transaction_id; ///< "Optional. Only valid if ChargingProfilePurpose is set to TxProfile, the transactionId MAY be used to match the profile to a specific transaction"
	int stack_level; ///< "Required. Value determining level in hierarchy stack of profiles. Higher values have precedence over lower values. Lowest level is 0"
	enum ocpp_charging_profile_purpose profile_purpose; ///< "Required. Defines the purpose of the schedule transferred by this message"
	enum ocpp_charging_profile_kind profile_kind; ///< "Required. Indicates the kind of schedule"
	enum ocpp_recurrency_kind * recurrency_kind; ///< "Optional. Indicates the start point of a recurrence"
	time_t valid_from; ///< "Optional. Point in time at which the profile starts to be valid. If absent, the profile is valid as soon as it is received by the ChargePoint."
	time_t valid_to; ///< "Optional. Point in time at which the profile stops to be valid. If absent, the profile is valid until it is replaced by another profile."
	struct ocpp_charging_schedule charging_schedule; ///< "Required. Contains limits for the available power or current overtime."
};

/**
 * @brief Allocates space in the list and adds period
 *
 * @param period_list list to extend
 * @param period the new period to append
 */
struct ocpp_charging_schedule_period_list * ocpp_extend_period_list(struct ocpp_charging_schedule_period_list * period_list, struct ocpp_charging_schedule_period * period);

/**
 * @brief Creates a charging profile from a JSON call
 *
 * @param csChargingProfiles a JSON object of type ChargingProfile
 * @param max_stack_level "Max StackLevel of a ChargingProfile. The number defined also indicates the max allowed number of installed charging schedules per Charging Profile Purposes."
 * @param allowed_charging_rate_units "A list of supported quantities for use in a ChargingSchedule. Allowed values: 'Current' and 'Power'"
 * @param max_periods "Maximum number of periods that may be defined per ChargingSchedule"
 * @param charging_profile_out output parameter for created charging profile
 * @param error_description_out string to write message to in case of error
 * @param error_description_length length of the error description string
 */
enum ocppj_err_t ocpp_charging_profile_from_json(cJSON * csChargingProfiles, int max_stack_level, const char * allowed_charging_rate_units,
					int max_periods, struct ocpp_charging_profile * charging_profile_out,
					char * error_description_out, size_t error_description_length);

/**
 * @brief allocates a buffer and copies the period_list into the new buffer
 * @description Caller is responsible for freeing the returned pointer.
 *
 * @param period_list original period_list to duplicate
 */
struct ocpp_charging_schedule_period_list * ocpp_duplicate_charging_schedule_period_list(const struct ocpp_charging_schedule_period_list * period_list);

/**
 * @brief allocates a buffer and copies the profile into the new buffer
 * @description Caller is responsible for freeing the returned pointer.
 *
 * @param profile original profile to duplicate
 */
struct ocpp_charging_profile * ocpp_duplicate_charging_profile(const struct ocpp_charging_profile * profile);

/**
 * @brief free memory allocated for a charging schedule
 *
 * @param charging_schedule The schedule to delete
 * @param with_reference If false it will only free the references within the schedule and not the schedule pointer itself.
 * The function should only be used with statically allocated schedules if with_reference is false;
 */
void ocpp_free_charging_schedule(struct ocpp_charging_schedule * charging_schedule, bool with_reference);

/**
 * @brief free memory allocated for a charging profile
 *
 * @param charging_profile profile to delete
 */
void ocpp_free_charging_profile(struct ocpp_charging_profile * charging_profile);

/**
 * @brief free memory allocated for period list
 *
 * @param periods list to delete
 */
void ocpp_free_charging_schedule_period_list(struct ocpp_charging_schedule_period_list * periods);

/**
 * @brief gets a reference to default profile used when no profile is active.
 *
 * @param purpose charging profile purpose of the default profile.
 */
struct ocpp_charging_profile * ocpp_get_default_charging_profile(enum ocpp_charging_profile_purpose purpose);

/**
 * @brief Converts a charging schedule to its JSON equivalent
 *
 * @param charging_schedule schedule to convert
 */
cJSON * ocpp_create_charging_schedule_json(struct ocpp_charging_schedule * charging_schedule);

/**
 * @brief checks if two periods result in the same charging values
 *
 * @param p1 period to compare
 * @param p2 period to compare
 */
bool ocpp_period_is_equal_charge(const struct ocpp_charging_schedule_period * p1, const struct ocpp_charging_schedule_period * p2);

/**
 * @brief converts configuration version of allowed charging rate units to type version used in ocpp calls.
 *
 * @description OCPP 1.5 Errata sheet mentions that ChargingScheduleAllowedChargingRateUnit should have had allowed
 * values 'A' and 'W'. This is the case in 2.0.1. But to not break compatibility with pre errata v4.0 'Current' and
 * 'Power' remains as the allowed values. This function converts 'Current' to 'A' and 'Power' to 'W' as a csl.
 * The function uses a static buffer for the result and only writes the buffer during the first call. Any new
 * call to this function will return result of the original call.
 */
const char * ocpp_get_allowed_charging_rate_units();
#endif /*OCPP_CHARGING_PROFILE_H*/
