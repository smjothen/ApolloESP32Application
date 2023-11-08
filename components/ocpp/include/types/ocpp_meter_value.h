#ifndef OCPP_METER_VALUE_H
#define OCPP_METER_VALUE_H

#include <stdbool.h>
#include <esp_err.h>

/** @file
* @brief Contains the OCPP type MeterValue and related functions
*/

/** @name ReadingContext
* @brief "Values of the context field of a value in SampledValue."
*/
///@{
#define OCPP_READING_CONTEXT_INTERRUPTION_BEGIN "Interruption.Begin"
#define OCPP_READING_CONTEXT_INTERRUPTION_END "Interruption.End"
#define OCPP_READING_CONTEXT_OTHER "Other"
#define OCPP_READING_CONTEXT_SAMPLE_CLOCK "Sample.Clock"
#define OCPP_READING_CONTEXT_SAMPLE_PERIODIC "Sample.Periodic"
#define OCPP_READING_CONTEXT_TRANSACTION_BEGIN "Transaction.Begin"
#define OCPP_READING_CONTEXT_TRANSACTION_END "Transaction.End"
#define OCPP_READING_CONTEXT_TRIGGER "Trigger"
///@}

/**
 * @brief Identifies the different reading contexts
 */
enum ocpp_reading_context_id{
	eOCPP_CONTEXT_INTERRUPTION_BEGIN = 1, ///< "Value taken at start of interruption."
	eOCPP_CONTEXT_INTERRUPTION_END, ///< "Value taken when resuming after interruption."
	eOCPP_CONTEXT_OTHER, ///< "Value for any other situations."
	eOCPP_CONTEXT_SAMPLE_CLOCK, ///< "Value taken at clock aligned interval."
	eOCPP_CONTEXT_SAMPLE_PERIODIC, ///< "Value taken as periodic sample relative to start time of transaction."
	eOCPP_CONTEXT_TRANSACTION_BEGIN, ///< "Value taken at start of transaction."
	eOCPP_CONTEXT_TRANSACTION_END, ///< "Value taken at end of transaction."
	eOCPP_CONTEXT_TRIGGER ///< "Value taken in response to a TriggerMessage.req"
};

/**
 * @brief converts id to reading context string
 *
 * @param id reading context id
 */
const char * ocpp_reading_context_from_id(enum ocpp_reading_context_id id);

/**
 * @brief converts context to id
 *
 * @param  context value to convert
 */
enum ocpp_reading_context_id ocpp_reading_context_to_id(const char * context);

/** @name ValueFormat
* @brief "Format that specifies how the value element in SampledValue is to be interpreted."
*/
///@{
#define OCPP_VALUE_FORMAT_RAW "Raw"
#define OCPP_VALUE_FORMAT_SIGNED_DATA "SignedData"
///@}

/**
 * @brief Identifies the different value formats
 */
enum ocpp_format_id{
	eOCPP_FORMAT_RAW = 1, ///< "Data is to be interpreted as integer/decimal numeric data."
	eOCPP_FORMAT_SIGNED_DATA ///< "Data is represented as a signed binary data block, encoded as hex data."
};

/**
 * @brief converts id to reading format
 *
 * @param id format id
 */
const char * ocpp_format_from_id(enum ocpp_format_id id);

/**
 * @brief converts format to id
 *
 * @param format value to convert
 */
enum ocpp_format_id ocpp_format_to_id(const char * format);

/** @name Measurand
* @brief "Allowable values of the optional "measurand" field of a Value element, as used in MeterValues.req and
* StopTransaction.req messages. Default value of "measurand" is always "Energy.Active.Import.Register""
*/
///@{
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
///@}

/**
 * @brief Identifies the different measurands
 */
enum ocpp_measurand_id{
	eOCPP_MEASURAND_CURRENT_EXPORT = 1, ///< "instantaneous current flow from EV"
	eOCPP_MEASURAND_CURRENT_IMPORT, ///< "instantaneous current flow to EV"
	eOCPP_MEASURAND_CURRENT_OFFERED, ///< "Maximum current offered to EV"
	/**
	 * @brief "Numerical value read from the "active electrical energy" (Wh or kWh) register of the (most authoritative)
	 * electrical meter measuring energy exported (to the grid)."
	 */
	eOCPP_MEASURAND_ENERGY_ACTIVE_EXPORT_REGISTER,
	/**
	 * @brief "Numerical value read from the "active electrical energy" (Wh or kWh) register of the (most authoritative)
	 * electrical meter measuring energy imported (from the grid supply)."
	 */
	eOCPP_MEASURAND_ENERGY_ACTIVE_IMPORT_REGISTER,
	/**
	 * @brief "Numerical value read from the "reactive electrical energy" (VARh or kVARh) register of the (most
	 * authoritative) electrical meter measuring energy exported (to the grid)."
	 */
	eOCPP_MEASURAND_ENERGY_REACTIVE_EXPORT_REGISTER,
	/**
	 * @brief "Numerical value read from the "reactive electrical energy" (VARh or kVARh) register of the (most
	 * authoritative) electrical meter measuring energy imported (from the grid supply)."
	 */
	eOCPP_MEASURAND_ENERGY_REACTIVE_IMPORT_REGISTER,
	/**
	 * @brief "Absolute amount of "active electrical energy" (Wh or kWh) exported (to the grid) during an associated time
	 * "interval", specified by a Metervalues ReadingContext, and applicable interval duration configuration values
	 * (in seconds) for "ClockAlignedDataInterval" and "MeterValueSampleInterval"."
	 */
	eOCPP_MEASURAND_ENERGY_ACTIVE_EXPORT_INTERVAL,
	/**
	 * @brief "Absolute amount of "active electrical energy" (Wh or kWh) imported (from the grid supply) during an
	 * associated time "interval", specified by a Metervalues ReadingContext, and applicable interval duration
	 * configuration values (in seconds) for "ClockAlignedDataInterval" and "MeterValueSampleInterval"."
	 */
	eOCPP_MEASURAND_ENERGY_ACTIVE_IMPORT_INTERVAL,
	/**
	 * @brief "Absolute amount of "reactive electrical energy" (VARh or kVARh) exported (to the grid) during an associated
	 * time "interval", specified by a Metervalues ReadingContext, and applicable interval duration configuration
	 * values (in seconds) for "ClockAlignedDataInterval" and "MeterValueSampleInterval"."
	 */
	eOCPP_MEASURAND_ENERGY_REACTIVE_EXPORT_INTERVAL,
	/**
	 * @brief "Absolute amount of "reactive electrical energy" (VARh or kVARh) imported (from the grid supply) during an
	 * associated time "interval", specified by a Metervalues ReadingContext, and applicable interval duration
	 * configuration values (in seconds) for "ClockAlignedDataInterval" and "MeterValueSampleInterval"."
	 */
	eOCPP_MEASURAND_ENERGY_REACTIVE_IMPORT_INTERVAL,
	/**
	 * @brief "Instantaneous reading of powerline frequency. NOTE: OCPP 1.6 does not have a UnitOfMeasure for
	 * frequency, the UnitOfMeasure for any SampledValue with measurand: Frequency is Hertz."
	 */
	eOCPP_MEASURAND_FREQUENCY,
	eOCPP_MEASURAND_POWER_ACTIVE_EXPORT, ///< "Instantaneous active power exported by EV. (W or kW)"
	eOCPP_MEASURAND_POWER_ACTIVE_IMPORT, ///< "Instantaneous active power imported by EV. (W or kW)"
	eOCPP_MEASURAND_POWER_FACTOR, ///< "Instantaneous power factor of total energy flow"
	eOCPP_MEASURAND_POWER_OFFERED, ///< "Maximum power offered to EV"
	eOCPP_MEASURAND_POWER_REACTIVE_EXPORT, ///< "Instantaneous reactive power exported by EV. (var or kvar)"
	eOCPP_MEASURAND_POWER_REACTIVE_IMPORT, ///< "Instantaneous reactive power imported by EV. (var or kvar)"
	eOCPP_MEASURAND_RPM, ///< "Fan speed in RPM"
	eOCPP_MEASURAND_SOC, ///< "State of charge of charging vehicle in percentage"
	eOCPP_MEASURAND_TEMPERATURE, ///< "Temperature reading inside Charge Point."
	eOCPP_MEASURAND_VOLTAGE ///< "Instantaneous AC RMS supply voltage"
};

/**
 * @brief converts id to measurand
 *
 * @param id measurand id
 */
const char * ocpp_measurand_from_id(enum ocpp_measurand_id id);

/**
 * @brief converts measurand to id
 *
 * @param  measurand value to convert
 */
enum ocpp_measurand_id ocpp_measurand_to_id(const char * measurand);


/**
 * @brief Indicate that the value is associated with a start time and end time.
 * @brief measurand The measurand to check
 */
bool ocpp_measurand_is_interval(const char * measurand);

/** @name Phase
* @brief "Phase as used in SampledValue. Phase specifies how a measured value is to be interpreted. Please note that not
* all values of Phase are applicable to all Measurands."
*/
///@{
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
///@}

/**
 * @brief Identifies the different phases
 */
enum ocpp_phase_id{
	eOCPP_PHASE_L1 = 1, ///< "Measured on L1"
	eOCPP_PHASE_L2, ///< "Measured on L2"
	eOCPP_PHASE_L3, ///< "Measured on L3"
	eOCPP_PHASE_N, ///< "Measured on Neutral"
	eOCPP_PHASE_L1_N, ///< "Measured on L1 with respect to Neutral conductor"
	eOCPP_PHASE_L2_N, ///< "Measured on L2 with respect to Neutral conductor"
	eOCPP_PHASE_L3_N, ///< "Measured on L3 with respect to Neutral conductor"
	eOCPP_PHASE_L1_L2, ///< "Measured between L1 and L2"
	eOCPP_PHASE_L2_L3, ///< "Measured between L2 and L3"
	eOCPP_PHASE_L3_L1 ///< "Measured between L3 and L1"
};

/**
 * @brief converts id to phase
 *
 * @param id phase id
 */
const char * ocpp_phase_from_id(enum ocpp_phase_id id);

/**
 * @brief converts phase to id
 *
 * @param phase value to convert
 */
enum ocpp_phase_id ocpp_phase_to_id(const char * phase);


/** @name Location
* @brief "Allowable values of the optional "location" field of a value element in SampledValue."
*/
///@{
#define OCPP_LOCATION_BODY "Body"
#define OCPP_LOCATION_CABLE "Cable"
#define OCPP_LOCATION_EV "EV"
#define OCPP_LOCATION_INLET "Inlet"
#define OCPP_LOCATION_OUTLET "Outlet"
///@}

/**
 * @brief Identifies the different locations
 */
enum ocpp_location_id{
	eOCPP_LOCATION_BODY = 1, ///< "Measurement inside body of Charge Point (e.g. Temperature)"
	eOCPP_LOCATION_CABLE, ///< "Measurement taken from cable between EV and Charge Point"
	eOCPP_LOCATION_EV, ///< "Measurement taken by EV"
	eOCPP_LOCATION_INLET, ///< "Measurement at network (“grid”) inlet connection"
	eOCPP_LOCATION_OUTLET, ///< "Measurement at a Connector. Default value"
};

/**
 * @brief converts id to location
 *
 * @param id location id
 */
const char * ocpp_location_from_id(enum ocpp_location_id id);
enum ocpp_location_id ocpp_location_to_id(const char * location);

/** @name UnitOfMeasure
* @brief "Allowable values of the optional "unit" field of a Value element, as used in SampledValue. Default value of "unit"
* is always "Wh"."
*/
///@{
#define OCPP_UNIT_OF_MEASURE_WH "Wh"
#define OCPP_UNIT_OF_MEASURE_KWH "kWh"
#define OCPP_UNIT_OF_MEASURE_VARH "varh"
#define OCPP_UNIT_OF_MEASURE_KVARH "kvarh"
#define OCPP_UNIT_OF_MEASURE_W "W"
#define OCPP_UNIT_OF_MEASURE_KW "kW"
#define OCPP_UNIT_OF_MEASURE_VA "VA"
#define OCPP_UNIT_OF_MEASURE_KVA "kVA"
#define OCPP_UNIT_OF_MEASURE_VAR "var"
#define OCPP_UNIT_OF_MEASURE_KVAR "kvar"
#define OCPP_UNIT_OF_MEASURE_A "A"
#define OCPP_UNIT_OF_MEASURE_V "V"
#define OCPP_UNIT_OF_MEASURE_CELSIUS "Celsius"
#define OCPP_UNIT_OF_MEASURE_FAHRENHEIT "Fahrenheit"
#define OCPP_UNIT_OF_MEASURE_K "K"
#define OCPP_UNIT_OF_MEASURE_PERCENT "Percent"
///@}

/**
 * @brief Identifies the different units of measurement
 */
enum ocpp_unit_id{
	eOCPP_UNIT_WH = 1, ///< "Watt-hours (energy). Default."
	eOCPP_UNIT_KWH, ///< "kiloWatt-hours (energy)."
	eOCPP_UNIT_VARH, ///< "Var-hours (reactive energy)."
	eOCPP_UNIT_KVARH, ///< "kilovar-hours (reactive energy)."
	eOCPP_UNIT_W, ///< "Watts (power)."
	eOCPP_UNIT_KW, ///< "kilowatts (power)."
	eOCPP_UNIT_VA, ///< "VoltAmpere (apparent power)."
	eOCPP_UNIT_KVA, ///< "kiloVolt Ampere (apparent power)."
	eOCPP_UNIT_VAR, ///< "Vars (reactive power)."
	eOCPP_UNIT_KVAR, ///< "kilovars (reactive power)."
	eOCPP_UNIT_A, ///< "Amperes (current)."
	eOCPP_UNIT_V, ///< "Voltage (r.m.s. AC)."
	eOCPP_UNIT_CELSIUS, ///< "Degrees (temperature)."
	eOCPP_UNIT_FAHRENHEIT, ///< "Degrees (temperature)."
	eOCPP_UNIT_K, ///< "Degrees Kelvin (temperature)."
	eOCPP_UNIT_PERCENT ///< "Percentage."
};

/**
 * @brief converts id to unit
 *
 * @param id unit id
 */
const char * ocpp_unit_from_id(enum ocpp_unit_id id);

/**
 * @brief converts unit to id
 *
 * @param unit value to convert
 */
enum ocpp_unit_id ocpp_unit_to_id(const char * unit);

/**
 * @brief "Single sampled value in MeterValues. Each value can be accompanied by optional fields."
 *
 * @todo: consider changing value size. Ocpp does not provide any limitation on length beyound 'String'.
 * The measurands that have been implemented use float and sprintf with %f (max 46 + '\0').
 * Changing the size of value might allow changes to the represntation of sampled value when written to file.
 */
struct ocpp_sampled_value{
	/**
	 * @brief "Required. Value as a “Raw” (decimal) number or “SignedData”. Field Type is
	 * “string” to allow for digitally signed data readings. Decimal numeric values are
	 * also acceptable to allow fractional values for measurands such as Temperature
	 * and Current."
	 */
	char value[64];
	uint8_t context; ///< "Optional. Type of detail value: start, end or sample. Default = “Sample.Periodic”"
	uint8_t format; ///< "Optional. Raw or signed data. Default = “Raw”"
	uint8_t measurand; ///< "Optional. Type of measurement. Default = “Energy.Active.Import.Register”"
	/**
	 * @brief Optional. indicates how the measured value is to be interpreted. For instance
	 * between L1 and neutral (L1-N) Please note that not all values of phase are
	 * applicable to all Measurands. When phase is absent, the measured value is
	 * interpreted as an overall value.
	 */
	uint8_t phase;
	uint8_t location; ///< "Optional. Location of measurement. Default=”Outlet”"
	uint8_t unit; ///< "Optional. Unit of the value. Default = “Wh” if the (default) measurand is an “Energy” type."
};

/**
 * @brief list of sampled values
 */
struct ocpp_sampled_value_list{
	struct ocpp_sampled_value * value; ///< sampled value
	struct ocpp_sampled_value_list * next; ///< next value or NULL if last
};

/**
 * @brief "Collection of one or more sampled values in MeterValues.req and StopTransaction.req. All sampled values in a
 * MeterValue are sampled at the same point in time."
 */
struct ocpp_meter_value{
	time_t timestamp; ///< "Required. Timestamp for measured value(s)."
	struct ocpp_sampled_value_list * sampled_value; ///< "Required. One or more measured values"
};

/**
 * @brief list of meter values
 */
struct ocpp_meter_value_list{
	struct ocpp_meter_value * value; ///< meter value
	struct ocpp_meter_value_list * next; ///< next value or NULL if last
};

// ocpp_sampled_value_list functions

/**
 * @brief allocates buffer for sampled value
 */
struct ocpp_sampled_value_list * ocpp_create_sampled_list();

/**
 * @brief free buffer used by sampled value list
 *
 * @param list the sampled value list to free
 */
void ocpp_sampled_list_delete(struct ocpp_sampled_value_list * list);

/**
 * @brief append a sampled value to the list
 *
 * @param list the list to append to
 * @param value the item to append
 */
struct ocpp_sampled_value_list * ocpp_sampled_list_add(struct ocpp_sampled_value_list * list, struct ocpp_sampled_value value);

/**
 * @brief get the last entry of the list
 *
 * @param list the list
 */
struct ocpp_sampled_value_list * ocpp_sampled_list_get_last(struct ocpp_sampled_value_list * list);

/**
 * @brief get nr of elements from current position in list
 *
 * @param list the list element to count from
 */
size_t ocpp_sampled_list_get_length(struct ocpp_sampled_value_list * list);

// ocpp_meter_value_list functions

/**
 * @brief allocate buffer for a meter value list
 */
struct ocpp_meter_value_list * ocpp_create_meter_list();

/**
 * @brief free buffer used by meter value list
 *
 * @param list the list to free
 */
void ocpp_meter_list_delete(struct ocpp_meter_value_list * list);

/**
 * @brief append meter value to list
 *
 * @param list the list to append to
 * @param value item to append
 */
struct ocpp_meter_value_list * ocpp_meter_list_add(struct ocpp_meter_value_list * list, struct ocpp_meter_value value);

/**
 * @brief append reference to meter value list instead of allocating new meter value
 *
 * @param list the list to append to
 * @param value item to append
 */
struct ocpp_meter_value_list * ocpp_meter_list_add_reference(struct ocpp_meter_value_list * list, struct ocpp_meter_value * value);

/**
 * @brief get last item in list
 *
 * @param list to get last item of
 */
struct ocpp_meter_value_list * ocpp_meter_list_get_last(struct ocpp_meter_value_list * list);

/**
 * @brief get the number of elements from list
 *
 * @param list the list to find last item from
 */
size_t ocpp_meter_list_get_length(struct ocpp_meter_value_list * list);

/**
 * @brief write the list as a short writable string for storage
 *
 * @param list the list to write as string
 * @param is_stop_txn_data if true then list should be for a StopTransaction.req else it is for MeterValues.req
 * @param buffer_length_out length of the written string
 */
unsigned char * ocpp_meter_list_to_contiguous_buffer(struct ocpp_meter_value_list * list, bool is_stop_txn_data, size_t * buffer_length_out);

/**
 * @brief converts a short string representing a meter value list to its list representation
 *
 * @param buffer the short string representing the list
 * @param buffer_length the length of the short string
 * @param is_stop_txn_data if true then list should be for a StopTransaction.req else it is for MeterValues.req
 */
struct ocpp_meter_value_list * ocpp_meter_list_from_contiguous_buffer(const unsigned char * buffer, size_t buffer_length, bool * is_stop_txn_data);

/**
 * @brief Indicate if the contiguous buffer is part of a StopTransaction without requiring allocation or covertation to meter list
 *
 * @param buffer the contiguous buffer to check
 * @param buffer_length the length of buffer
 * @param is_stop_txn_data_out output parameter for the result
 *
 * @return ESP_OK if result is valid, ESP_ERR_INVALID_ARG if buffer or length is invalid
 */
esp_err_t ocpp_is_stop_txn_data_from_contiguous_buffer(const unsigned char * buffer, size_t buffer_length, bool * is_stop_txn_data_out);

#endif /*OCPP_METER_VALUE_H*/
