#ifndef OCPP_METER_VALUE_H
#define OCPP_METER_VALUE_H

#include <stdbool.h>

/*
 * The structures/values defined here apart from sampled value are only directly or indirectly by
 * the meter_value type in the ocpp specification.
 */

#define OCPP_READING_CONTEXT_INTERRUPTION_BEGIN "Interruption.Begin"
#define OCPP_READING_CONTEXT_INTERRUPTION_END "Interruption.End"
#define OCPP_READING_CONTEXT_OTHER "Other"
#define OCPP_READING_CONTEXT_SAMPLE_CLOCK "Sample.Clock"
#define OCPP_READING_CONTEXT_SAMPLE_PERIODIC "Sample.Periodic"
#define OCPP_READING_CONTEXT_TRANSACTION_BEGIN "Transaction.Begin"
#define OCPP_READING_CONTEXT_TRANSACTION_END "Transaction.End"
#define OCPP_READING_CONTEXT_TRIGGER "Trigger"

enum ocpp_reading_context_id{
	eOCPP_CONTEXT_INTERRUPTION_BEGIN = 1,
	eOCPP_CONTEXT_INTERRUPTION_END,
	eOCPP_CONTEXT_OTHER,
	eOCPP_CONTEXT_SAMPLE_CLOCK,
	eOCPP_CONTEXT_SAMPLE_PERIODIC,
	eOCPP_CONTEXT_TRANSACTION_BEGIN,
	eOCPP_CONTEXT_TRANSACTION_END,
	eOCPP_CONTEXT_TRIGGER
};

const char * ocpp_reading_context_from_id(enum ocpp_reading_context_id id);
enum ocpp_reading_context_id ocpp_reading_context_to_id(const char * context);

#define OCPP_VALUE_FORMAT_RAW "Raw"
#define OCPP_VALUE_FORMAT_SIGNED_DATA "SignedData"

enum ocpp_format_id{
	eOCPP_FORMAT_RAW = 1,
	eOCPP_FORMAT_SIGNED_DATA
};

const char * ocpp_format_from_id(enum ocpp_format_id id);
enum ocpp_format_id ocpp_format_to_id(const char * format);

#define OCPP_MEASURAND_CURRENT_EXPORT "Current.Export"
#define OCPP_MEASURAND_CURRENT_IMPORT "Current.Import"
#define OCPP_MEASURAND_CURRENT_OFFERED "Current.Offered"
#define OCPP_MEASURAND_ENERGY_ACTIVE_EXPORT_REGISTER "Energy.Active.Export.Register"
#define OCPP_MEASURAND_ENERGY_ACTIVE_IMPORT_REGISTER "Energy.Active.Import.Register"
#define OCPP_MEASURAND_ENERGY_REACTIVE_EXPORT_REGISTER "Energy.Reactive.Export.Register"
#define OCPP_MEASURAND_ENERGY_REACTIVE_IMPORT_REGISTER "Energy.Reactive.Import.Register"
#define OCPP_MEASURAND_ENERGY_ACTIVE_EXPORT_INTERVAL "Energy.Active.Export.Interval"
#define OCPP_MEASURAND_ENERGY_ACTIVE_IMPORT_INTERVAL "Energy.Active.Import.Interval"
#define OCPP_MEASURAND_ENERGY_REACTIVE_EXPORT_INTERVAL "Energy.Reactive.Export.Interval"
#define OCPP_MEASURAND_ENERGY_REACTIVE_IMPORT_INTERVAL "Energy.Reactive.Import.Interval"
#define OCPP_MEASURAND_FREQUENCY "Frequency"
#define OCPP_MEASURAND_POWER_ACTIVE_EXPORT "Power.Active.Export"
#define OCPP_MEASURAND_POWER_ACTIVE_IMPORT "Power.Active.Import"
#define OCPP_MEASURAND_POWER_FACTOR "Power.Factor"
#define OCPP_MEASURAND_POWER_OFFERED "Power.Offered"
#define OCPP_MEASURAND_POWER_REACTIVE_EXPORT "Power.Reactive.Export"
#define OCPP_MEASURAND_POWER_REACTIVE_IMPORT "Power.Reactive.Import"
#define OCPP_MEASURAND_RPM "RPM"
#define OCPP_MEASURAND_SOC "SoC"
#define OCPP_MEASURAND_TEMPERATURE "Temperature"
#define OCPP_MEASURAND_VOLTAGE "Voltage"

enum ocpp_measurand_id{
	eOCPP_MEASURAND_CURRENT_EXPORT = 1,
	eOCPP_MEASURAND_CURRENT_IMPORT,
	eOCPP_MEASURAND_CURRENT_OFFERED,
	eOCPP_MEASURAND_ENERGY_ACTIVE_EXPORT_REGISTER,
	eOCPP_MEASURAND_ENERGY_ACTIVE_IMPORT_REGISTER,
	eOCPP_MEASURAND_ENERGY_REACTIVE_EXPORT_REGISTER,
	eOCPP_MEASURAND_ENERGY_REACTIVE_IMPORT_REGISTER,
	eOCPP_MEASURAND_ENERGY_ACTIVE_EXPORT_INTERVAL,
	eOCPP_MEASURAND_ENERGY_ACTIVE_IMPORT_INTERVAL,
	eOCPP_MEASURAND_ENERGY_REACTIVE_EXPORT_INTERVAL,
	eOCPP_MEASURAND_ENERGY_REACTIVE_IMPORT_INTERVAL,
	eOCPP_MEASURAND_FREQUENCY,
	eOCPP_MEASURAND_POWER_ACTIVE_EXPORT,
	eOCPP_MEASURAND_POWER_ACTIVE_IMPORT,
	eOCPP_MEASURAND_POWER_FACTOR,
	eOCPP_MEASURAND_POWER_OFFERED,
	eOCPP_MEASURAND_POWER_REACTIVE_EXPORT,
	eOCPP_MEASURAND_POWER_REACTIVE_IMPORT,
	eOCPP_MEASURAND_RPM,
	eOCPP_MEASURAND_SOC,
	eOCPP_MEASURAND_TEMPERATURE,
	eOCPP_MEASURAND_VOLTAGE
};

const char * ocpp_measurand_from_id(enum ocpp_measurand_id id);
enum ocpp_measurand_id ocpp_measurand_to_id(const char * measurand);

#define OCPP_PHASE_L1 "L1"
#define OCPP_PHASE_L2 "L2"
#define OCPP_PHASE_L3 "L3"
#define OCPP_PHASE_N "N"
#define OCPP_PHASE_L1_N "L1-N"
#define OCPP_PHASE_L2_N "L2-N"
#define OCPP_PHASE_L3_N "L3-N"
#define OCPP_PHASE_L1_L2 "L1-L2"
#define OCPP_PHASE_L2_L3 "L2-L3"
#define OCPP_PHASE_L3_L1 "L3-L1"

enum ocpp_phase_id{
	eOCPP_PHASE_L1 = 1,
	eOCPP_PHASE_L2,
	eOCPP_PHASE_L3,
	eOCPP_PHASE_N,
	eOCPP_PHASE_L1_N,
	eOCPP_PHASE_L2_N,
	eOCPP_PHASE_L3_N,
	eOCPP_PHASE_L1_L2,
	eOCPP_PHASE_L2_L3,
	eOCPP_PHASE_L3_L1
};

const char * ocpp_phase_from_id(enum ocpp_phase_id id);
enum ocpp_phase_id ocpp_phase_to_id(const char * phase);

#define OCPP_LOCATION_BODY "Body"
#define OCPP_LOCATION_CABLE "Cable"
#define OCPP_LOCATION_EV "EV"
#define OCPP_LOCATION_INLET "Inlet"
#define OCPP_LOCATION_OUTLET "Outlet"

enum ocpp_location_id{
	eOCPP_LOCATION_BODY = 1,
	eOCPP_LOCATION_CABLE,
	eOCPP_LOCATION_EV,
	eOCPP_LOCATION_INLET,
	eOCPP_LOCATION_OUTLET,
};

const char * ocpp_location_from_id(enum ocpp_location_id id);
enum ocpp_location_id ocpp_location_to_id(const char * location);

#define OCPP_UNIT_OF_MEASURE_WH "Wh" //(default)
#define OCPP_UNIT_OF_MEASURE_KWH "kWh"
#define OCPP_UNIT_OF_MEASURE_VARH "varh"
#define OCPP_UNIT_OF_MEASURE_KVARH "kvarh"
#define OCPP_UNIT_OF_MEASURE_W "W"
#define OCPP_UNIT_OF_MEASURE_KW "kW"
#define OCPP_UNIT_OF_MEASURE_VA "VA"
#define OCPP_UNIT_OF_MEASURE_KVA "kVA"
#define OCPP_UNIT_OF_MEASURE_KVAR "kvar"
#define OCPP_UNIT_OF_MEASURE_A "A"
#define OCPP_UNIT_OF_MEASURE_V "V"
#define OCPP_UNIT_OF_MEASURE_CELSIUS "Celsius"
#define OCPP_UNIT_OF_MEASURE_FAHRENHEIT "Fahrenheit"
#define OCPP_UNIT_OF_MEASURE_K "K"
#define OCPP_UNIT_OF_MEASURE_PERCENT "Percent"

enum ocpp_unit_id{
	eOCPP_UNIT_WH = 1,
	eOCPP_UNIT_KWH,
	eOCPP_UNIT_VARH,
	eOCPP_UNIT_KVARH,
	eOCPP_UNIT_W,
	eOCPP_UNIT_KW,
	eOCPP_UNIT_VA,
	eOCPP_UNIT_KVA,
	eOCPP_UNIT_KVAR,
	eOCPP_UNIT_A,
	eOCPP_UNIT_V,
	eOCPP_UNIT_CELSIUS,
	eOCPP_UNIT_FAHRENHEIT,
	eOCPP_UNIT_K,
	eOCPP_UNIT_PERCENT
};

const char * ocpp_unit_from_id(enum ocpp_unit_id id);
enum ocpp_unit_id ocpp_unit_to_id(const char * unit);

/*
 *TODO: consider changing value size. Ocpp does not provide any limitation on length beyound 'String'.
 * The measurands that have been implemented use float and sprintf with %f (max 46 + '\0').
 * Changing the size of value might allow changes to the represntation of sampled value when written to file.
 */
struct ocpp_sampled_value{
	char value[64];
	uint8_t context;
	uint8_t format;
	uint8_t measurand;
	uint8_t phase;
	uint8_t location;
	uint8_t unit;
};

struct ocpp_sampled_value_list{
	struct ocpp_sampled_value * value;
	struct ocpp_sampled_value_list * next;
};

struct ocpp_meter_value{
	time_t timestamp;
	struct ocpp_sampled_value_list * sampled_value;
};

struct ocpp_meter_value_list{
	struct ocpp_meter_value * value;
	struct ocpp_meter_value_list * next;
};

// ocpp_sampled_value_list functions
struct ocpp_sampled_value_list * ocpp_create_sampled_list();
void ocpp_sampled_list_delete(struct ocpp_sampled_value_list * list);
struct ocpp_sampled_value_list * ocpp_sampled_list_add(struct ocpp_sampled_value_list * list, struct ocpp_sampled_value value);
struct ocpp_sampled_value_list * ocpp_sampled_list_get_last(struct ocpp_sampled_value_list * list);
size_t ocpp_sampled_list_get_length(struct ocpp_sampled_value_list * list);

// ocpp_meter_value_list functions
struct ocpp_meter_value_list * ocpp_create_meter_list();
void ocpp_meter_list_delete(struct ocpp_meter_value_list * list);
struct ocpp_meter_value_list * ocpp_meter_list_add(struct ocpp_meter_value_list * list, struct ocpp_meter_value value);
struct ocpp_meter_value_list * ocpp_meter_list_add_reference(struct ocpp_meter_value_list * list, struct ocpp_meter_value * value);
struct ocpp_meter_value_list * ocpp_meter_list_get_last(struct ocpp_meter_value_list * list);
size_t ocpp_meter_list_get_length(struct ocpp_meter_value_list * list);
unsigned char * ocpp_meter_list_to_contiguous_buffer(struct ocpp_meter_value_list * list, bool is_stop_txn_data, size_t * buffer_length_out);
struct ocpp_meter_value_list * ocpp_meter_list_from_contiguous_buffer(const unsigned char * buffer, size_t buffer_length, bool * is_stop_txn_data);

#endif /*OCPP_METER_VALUE_H*/
