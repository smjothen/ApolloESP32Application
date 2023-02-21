#ifndef OCPP_KEY_VALUE_H
#define OCPP_KEY_VALUE_H

#include <stdbool.h>

#include <cJSON.h>

/** @file
* @brief Contains the OCPP type KeyValue, the related configuration value and helper functions
*/

/** @name Configuration key
* @brief Identifiers for each configuration value
*/
///@{
// Core profile keys
#define OCPP_CONFIG_KEY_ALLOW_OFFLINE_TX_FOR_UNKNOWN_ID "AllowOfflineTxForUnknownId"
#define OCPP_CONFIG_KEY_AUTHORIZATION_CACHE_ENABLED "AuthorizationCacheEnabled"
#define OCPP_CONFIG_KEY_AUTHORIZE_REMOTE_TX_REQUESTS "AuthorizeRemoteTxRequests"
#define OCPP_CONFIG_KEY_BLINK_REPEAT "BlinkRepeat"
#define OCPP_CONFIG_KEY_CLOCK_ALIGNED_DATA_INTERVAL "ClockAlignedDataInterval"
#define OCPP_CONFIG_KEY_CONNECTION_TIMEOUT "ConnectionTimeOut"
#define OCPP_CONFIG_KEY_CONNECTOR_PHASE_ROTATION "ConnectorPhaseRotation"
#define OCPP_CONFIG_KEY_CONNECTOR_PHASE_ROTATION_MAX_LENGTH "ConnectorPhaseRotationMaxLength"
#define OCPP_CONFIG_KEY_GET_CONFIGURATION_MAX_KEYS "GetConfigurationMaxKeys"
#define OCPP_CONFIG_KEY_HEARTBEAT_INTERVAL "HeartbeatInterval"
#define OCPP_CONFIG_KEY_LIGHT_INTENSITY "LightIntensity"
#define OCPP_CONFIG_KEY_LOCAL_AUTHORIZE_OFFLINE "LocalAuthorizeOffline"
#define OCPP_CONFIG_KEY_LOCAL_PRE_AUTHORIZE "LocalPreAuthorize"
#define OCPP_CONFIG_KEY_MAX_ENERGY_ON_INVALID_ID "MaxEnergyOnInvalidId"
#define OCPP_CONFIG_KEY_MESSAGE_TIMEOUT "MessageTimeout"
#define OCPP_CONFIG_KEY_METER_VALUES_ALIGNED_DATA "MeterValuesAlignedData"
#define OCPP_CONFIG_KEY_METER_VALUES_ALIGNED_DATA_MAX_LENGTH "MeterValuesAlignedDataMaxLength"
#define OCPP_CONFIG_KEY_METER_VALUES_SAMPLED_DATA "MeterValuesSampledData"
#define OCPP_CONFIG_KEY_METER_VALUES_SAMPLED_DATA_MAX_LENGTH "MeterValuesSampledDataMaxLength"
#define OCPP_CONFIG_KEY_METER_VALUE_SAMPLE_INTERVAL "MeterValueSampleInterval"
#define OCPP_CONFIG_KEY_MINIMUM_STATUS_DURATION "MinimumStatusDuration"
#define OCPP_CONFIG_KEY_NUMBER_OF_CONNECTORS "NumberOfConnectors"
#define OCPP_CONFIG_KEY_RESET_RETRIES "ResetRetries"
#define OCPP_CONFIG_KEY_STOP_TRANSACTION_MAX_METER_VALUES "StopTransactionMaxMeterValues"
#define OCPP_CONFIG_KEY_STOP_TRANSACTION_ON_EV_SIDE_DISCONNECT "StopTransactionOnEVSideDisconnect"
#define OCPP_CONFIG_KEY_STOP_TRANSACTION_ON_INVALID_ID "StopTransactionOnInvalidId"
#define OCPP_CONFIG_KEY_STOP_TXN_ALIGNED_DATA "StopTxnAlignedData"
#define OCPP_CONFIG_KEY_STOP_TXN_ALIGNED_DATA_MAX_LENGTH "StopTxnAlignedDataMaxLength"
#define OCPP_CONFIG_KEY_STOP_TXN_SAMPLED_DATA "StopTxnSampledData"
#define OCPP_CONFIG_KEY_STOP_TXN_SAMPLED_DATA_MAX_LENGTH "StopTxnSampledDataMaxLength"
#define OCPP_CONFIG_KEY_SUPPORTED_FEATURE_PROFILES "SupportedFeatureProfiles"
#define OCPP_CONFIG_KEY_SUPPORTED_FEATURE_PROFILES_MAX_LENGTH "SupportedFeatureProfilesMaxLength"
#define OCPP_CONFIG_KEY_TRANSACTION_MESSAGE_ATTEMPTS "TransactionMessageAttempts"
#define OCPP_CONFIG_KEY_TRANSACTION_MESSAGE_RETRY_INTERVAL "TransactionMessageRetryInterval"
#define OCPP_CONFIG_KEY_UNLOCK_CONNECTOR_ON_EV_SIDE_DISCONNECT "UnlockConnectorOnEVSideDisconnect"
#define OCPP_CONFIG_KEY_WEBSOCKET_PING_INTERVAL "WebSocketPingInterval"
// Firmware management profile
#define OCPP_CONFIG_KEY_SUPPORTED_FILE_TRANSFER_PROTOCOLS "SupportedFileTransferProtocols"
// local auth list management profile
#define OCPP_CONFIG_KEY_LOCAL_AUTH_LIST_ENABLED "LocalAuthListEnabled"
#define OCPP_CONFIG_KEY_LOCAL_AUTH_LIST_MAX_LENGTH "LocalAuthListMaxLength"
#define OCPP_CONFIG_KEY_SEND_LOCAL_LIST_MAX_LENGTH "SendLocalListMaxLength"
// Reservation profile
#define OCPP_CONFIG_KEY_RESERVE_CONNECTOR_ZERO_SUPPORTED "ReserveConnectorZeroSupported"
// Smart charging profile
#define OCPP_CONFIG_KEY_CHARGE_PROFILE_MAX_STACK_LEVEL "ChargeProfileMaxStackLevel"
#define OCPP_CONFIG_KEY_CHARGING_SCHEDULE_ALLOWED_CHARGING_RATE_UNIT "ChargingScheduleAllowedChargingRateUnit"
#define OCPP_CONFIG_KEY_CHARGING_SCHEDULE_MAX_PERIODS "ChargingScheduleMaxPeriods"
#define OCPP_CONFIG_KEY_CONNECTOR_SWITCH_3_TO_1_PHASE_SUPPORTED "ConnectorSwitch3to1PhaseSupported"
#define OCPP_CONFIG_KEY_MAX_CHARGING_PROFILES_INSTALLED "MaxChargingProfilesInstalled"
///@}

/**
 * @brief check if given key is a valid key
 *
 * @param key value to check if valid.
 */
bool is_configuration_key(const char * key);

#endif /*OCPP_KEY_VALUE_H*/
