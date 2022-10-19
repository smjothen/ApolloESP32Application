#ifndef OCPP_CHARGING_PROFILE_H
#define OCPP_CHARGING_PROFILE_H

#include <stdint.h>
#include <time.h>

#include "cJSON.h"

#include "ocpp_json/ocppj_message_structure.h"

#define OCPP_CHARGING_PROFILE_PURPOSE_CHARGE_POINT_MAX "ChargePointMaxProfile"
#define	OCPP_CHARGING_PROFILE_PURPOSE_TX_DEFAULT "TxDefaultProfile"
#define	OCPP_CHARGING_PROFILE_PURPOSE_TX "TxProfile"

enum ocpp_charging_profile_purpose{
	eOCPP_CHARGING_PROFILE_PURPOSE_CHARGE_POINT_MAX,
	eOCPP_CHARGING_PROFILE_PURPOSE_TX_DEFAULT,
	eOCPP_CHARGING_PROFILE_PURPOSE_TX,
};

enum ocpp_charging_profile_purpose ocpp_charging_profile_purpose_to_id(const char * purpose);

#define OCPP_CHARGING_PROFILE_KIND_ABSOLUTE "Absolute"
#define OCPP_CHARGING_PROFILE_KIND_RECURRING "Recurring"
#define OCPP_CHARGING_PROFILE_KIND_RELATIVE "Relative"

enum ocpp_charging_profile_kind{
	eOCPP_CHARGING_PROFILE_KIND_ABSOLUTE,
	eOCPP_CHARGING_PROFILE_KIND_RECURRING,
	eOCPP_CHARGING_PROFILE_KIND_RELATIVE
};

enum ocpp_charging_profile_kind ocpp_charging_profile_kind_to_id(const char * profile_kind);

#define OCPP_RECURRENCY_KIND_DAILY "Daily"
#define OCPP_RECURRENCY_KIND_WEEKLY "Weekly"

enum ocpp_recurrency_kind{
	eOCPP_RECURRENCY_KIND_DAILY,
	eOCPP_RECURRENCY_KIND_WEEKLY
};

enum ocpp_recurrency_kind ocpp_recurrency_kind_to_id(const char * recurrency_kind);

#define OCPP_CHARGING_RATE_W "W"
#define OCPP_CHARGING_RATE_A "A"

enum ocpp_charging_rate_unit{
	eOCPP_CHARGING_RATE_W,
	eOCPP_CHARGING_RATE_A
};

enum ocpp_charging_rate_unit ocpp_charging_rate_unit_to_id(const char * unit);
const char * ocpp_charging_rate_unit_from_id(const enum ocpp_charging_rate_unit charge_rate_unit);

struct ocpp_charging_schedule_period{
	int start_period;
	float limit;
	int number_phases;
};

struct ocpp_charging_schedule_period_list{
	struct ocpp_charging_schedule_period value;
	struct ocpp_charging_schedule_period_list * next;
};


struct ocpp_charging_schedule{
	int * duration;
	time_t * start_schedule;
	enum ocpp_charging_rate_unit charge_rate_unit;
	struct ocpp_charging_schedule_period_list schedule_period;
	float min_charging_rate;
};

struct ocpp_charging_profile{
	int profile_id;
	int * transaction_id;
	int stack_level;
	enum ocpp_charging_profile_purpose profile_purpose;
	enum ocpp_charging_profile_kind profile_kind;
	enum ocpp_recurrency_kind * recurrency_kind;
	time_t valid_from;
	time_t valid_to;
	struct ocpp_charging_schedule charging_schedule;
};

enum ocppj_err_t ocpp_charging_profile_from_json(cJSON * csChargingProfiles, int max_stack_level, const char * allowed_charging_rate_units,
					int max_periods, struct ocpp_charging_profile * charging_profile_out,
					char * error_description_out, size_t error_description_length);

void ocpp_free_charging_schedule(struct ocpp_charging_schedule * charging_schedule);
void ocpp_free_charging_profile(struct ocpp_charging_profile * charging_profile);
struct ocpp_charging_profile * ocpp_get_default_charging_profile(enum ocpp_charging_profile_purpose purpose);

cJSON * ocpp_create_charging_schedule_json(struct ocpp_charging_schedule * charging_schedule);
#endif /*OCPP_CHARGING_PROFILE_H*/
