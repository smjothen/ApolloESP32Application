#ifndef ZAPTEC_PROTOCOL_WARNINGS_H
#define ZAPTEC_PROTOCOL_WARNINGS_H

// Environment
#define WARNING_HUMIDITY            (1l <<  0)
#define WARNING_TEMPERATURE         (1l <<  1)
#define WARNING_TEMPERATURE_ERROR   (1l <<  2) // If the temperature sensor can't be read

#define WARNING_OVERCURRENT_INSTALLATION (1l << 20)
//#define WARNING_FPGA_UNEXPECTED_RELAY (1l << 21)
#define WARNING_UNEXPECTED_RELAY    (1l << 21)
#define WARNING_O_PEN               (1l << 22)
//#define WARNING_FPGA_CHARGING_RESET   (1l << 22)
#define WARNING_FPGA_WATCHDOG         (1l << 28)

#define WARNING_NO_SWITCH_POW_DEF   (1l <<  9)
    
// eMeter
#define WARNING_EMETER_NO_RESPONSE  (1l <<  3)
#define WARNING_EMETER_LINK         (1l << 25)

#define WARNING_EMETER_ALARM        (1l << 24) // Cause will be written to diagnostics observation

// Charging
#define WARNING_CHARGE_OVERCURRENT  (1l <<  5)

#define WARNING_NO_VOLTAGE_L1       (1l << 26)
#define WARNING_NO_VOLTAGE_L2_L3    (1l << 27)
    
#define WARNING_MAX_SESSION_RESTART (1l <<  4)

// Power    
#define WARNING_12V_LOW_LEVEL       (1l <<  7)
    
// Car state
#define WARNING_PILOT_STATE         (1l <<  6)

#define WARNING_PILOT_LOW_LEVEL     (1l <<  8)
#define WARNING_PILOT_NO_PROXIMITY  (1l << 23)

// Charger state
#define WARNING_REBOOT              (1l << 10)
#define WARNING_DISABLED            (1l << 11)
//#define WARNING_VARISCITE           (1l << 31)
#define WARNING_FPGA_VERSION         (1l << 31)

// RCD    
#define WARNING_RCD_6MA             (1l << 12)
#define WARNING_RCD_30MA              (1l << 13)
//#define WARNING_RCD_PEAK            (1l << 14)
#define WARNING_RCD_TEST_6MA        (1l << 16)
#define WARNING_RCD_TEST_30MA       (1l << 17)
#define WARNING_RCD_FAILURE         (1l << 18)
//#define WARNING_RCD_TEST_TIMEOUT    (1l << 19)
    
#define WARNING_SERVO               (1l << 29)
    
// RCD Warnings: These are cleared when a car is disconnected
#define WARNING_RCD                 (WARNING_RCD_6MA | WARNING_RCD_30MA | WARNING_RCD_TEST_6MA | WARNING_RCD_TEST_30MA | WARNING_RCD_FAILURE)

// Soft-stop warnings lets the car stop gracefully. The relays will be forcefully opened after 5 seconds
#define WARNING_SOFT_STOP           (WARNING_HUMIDITY | WARNING_TEMPERATURE | WARNING_TEMPERATURE_ERROR | WARNING_REBOOT | WARNING_DISABLED | WARNING_EMETER_LINK | WARNING_CHARGE_OVERCURRENT | WARNING_NO_VOLTAGE_L1 | WARNING_NO_VOLTAGE_L2_L3)

// These warnings are cleared when the car is disconnected, then reconnected
#define WARNING_CLEAR_REPLUG        (WARNING_NO_VOLTAGE_L1 | WARNING_NO_VOLTAGE_L2_L3)

// These warnings are cleared once at the transition into the Disconnected-state
#define WARNING_CLEAR_DISCONNECT_TRANSITION  (WARNING_RCD | WARNING_SERVO | WARNING_EMETER_ALARM)
    
// These warnings are continuously cleared when no car is connected
#define WARNING_CLEAR_DISCONNECT    (WARNING_CHARGE_OVERCURRENT | WARNING_OVERCURRENT_INSTALLATION | WARNING_PILOT_STATE | WARNING_PILOT_LOW_LEVEL | WARNING_PILOT_NO_PROXIMITY | WARNING_MAX_SESSION_RESTART)

#define EMETER_PARAM_STATUS_DRDY     (1l << 16) // New low rate results (data) ready
#define EMETER_PARAM_STATUS_OV_FREQ  (1l << 15) // Frequency over High Limit
#define EMETER_PARAM_STATUS_UN_FREQ  (1l << 14) // Under Low Frequency Limit
#define EMETER_PARAM_STATUS_OV_TEMP  (1l << 13) // Temperature over High Limit
#define EMETER_PARAM_STATUS_UN_TEMP  (1l << 12) // Under Low Temperature Limit
#define EMETER_PARAM_STATUS_OV_VRMSC (1l << 11) // RMS Voltage C Over Limit
#define EMETER_PARAM_STATUS_UN_VRMSC (1l << 10) // RMS Voltage C Under Limit
#define EMETER_PARAM_STATUS_OV_VRMSB (1l << 9) // RMS Voltage B Over Limit
#define EMETER_PARAM_STATUS_UN_VRMSB (1l << 8) // RMS Voltage B Under Limit
#define EMETER_PARAM_STATUS_OV_VRMSA (1l << 7) // RMS Voltage A Over Limit
#define EMETER_PARAM_STATUS_UN_VRMSA (1l << 6) // RMS Voltage A Under Limit
#define EMETER_PARAM_STATUS_UN_PFC   (1l << 5) // Power Factor C Under Limit
#define EMETER_PARAM_STATUS_UN_PFB   (1l << 4) // Power Factor B Under Limit
#define EMETER_PARAM_STATUS_UN_PFA   (1l << 3) // Power Factor A Under Limit
#define EMETER_PARAM_STATUS_OV_IRMSC (1l << 2) // RMS Current C Over Limit
#define EMETER_PARAM_STATUS_OV_IRMSB (1l << 1) // RMS Current B Over Limit
#define EMETER_PARAM_STATUS_OV_IRMSA (1l << 0) // RMS Current A Over Limit

#endif /* ZAPTEC_PROTOCOL_WARNINGS_H */
