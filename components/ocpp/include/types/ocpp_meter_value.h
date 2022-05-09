#ifndef OCPP_METER_VALUE_H
#define OCPP_METER_VALUE_H

/*
 * The structures/values defined here apart from sampled value are only directly or indirectly by
 * the meter_value type in the ocpp specification.
 */

#define OCPP_READING_CONTEXT_INTERRUPT_BEGIN "Interruption.Begin"
#define OCPP_READING_CONTEXT_INTERRUPT_END "Interruption.End"
#define OCPP_READING_CONTEXT_OTHER "Other"
#define OCPP_READING_CONTEXT_SAMPLE_CLOCK "Sample.Clock"
#define OCPP_READING_CONTEXT_SAMPLE_PERIODIC "Sample.Periodic"
#define OCPP_READING_CONTEXT_TRANSACTION_BEGIN "Transaction.Begin"
#define OCPP_READING_CONTEXT_TRANSACTION_END "Transaction.End"
#define OCPP_READING_CONTEXT_TRIGGER "Trigger"

#define OCPP_VALUE_FORMAT_RAW "Raw"
#define OCPP_VALUE_FORMAT_SIGNED_DATA "SignedData"

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
#define OCPP_MEASURAND_TEMERATURE "Temperature"
#define OCPP_MEASURAND_VOLTAGE "Voltage"

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

#define OCPP_LOCATION_BODY "Body"
#define OCPP_LOCATION_CABLE "Cable"
#define OCPP_LOCATION_EV "EV"
#define OCPP_LOCATION_INLET "Inlet"
#define OCPP_LOCATION_OUTLET "Outlet"

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

struct ocpp_sampled_value{
	const char * value;
	const char * context;
	const char * format;
	const char * measurand;
	const char * phase;
	const char * location;
	const char * unit;
};

struct ocpp_meter_value{
	time_t timestamp;
	struct ocpp_sampled_value sampled_value;
};

cJSON * create_meter_value_json(struct ocpp_meter_value meter_value);

#endif /*OCPP_METER_VALUE_H*/
