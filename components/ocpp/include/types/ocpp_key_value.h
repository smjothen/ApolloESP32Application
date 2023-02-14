#ifndef OCPP_KEY_VALUE_H
#define OCPP_KEY_VALUE_H

#include <stdbool.h>

#include <cJSON.h>

/** @file
* @brief Contains the OCPP type KeyValue, the related configuration value and helper functions
*/

/** @name KeyValue
* @brief Identifiers for each configuration value
*/
///@{
// Core profile keys
#define OCPP_CONFIG_KEY_AUTHORIZE_REMOTE_TX_REQUESTS "AuthorizeRemoteTxRequests"
#define OCPP_CONFIG_KEY_CLOCK_ALIGNED_DATA_INTERVAL "ClockAlignedDataInterval"
#define OCPP_CONFIG_KEY_CONNECTION_TIMEOUT "ConnectionTimeOut"
#define OCPP_CONFIG_KEY_CONNECTOR_PHASE_ROTATION "ConnectorPhaseRotation"
#define OCPP_CONFIG_KEY_CONNECTOR_PHASE_ROTATION_MAX_LENGTH "ConnectorPhaseRotationMaxLength"
#define OCPP_CONFIG_KEY_GET_CONFIGURATION_MAX_KEYS "GetConfigurationMaxKeys"
#define OCPP_CONFIG_KEY_HEARTBEAT_INTERVAL "HeartbeatInterval"
#define OCPP_CONFIG_KEY_LIGHT_INTENSITY "LightIntensity"
#define OCPP_CONFIG_KEY_LOCAL_AUTHORIZE_OFFLINE "LocalAuthorizeOffline"
#define OCPP_CONFIG_KEY_LOCAL_PRE_AUTHORIZE "LocalPreAuthorize"
#define OCPP_CONFIG_KEY_METER_VALUES_ALIGNED_DATA "MeterValuesAlignedData"
#define OCPP_CONFIG_KEY_METER_VALUES_ALIGNED_DATA_MAX_LENGTH "MeterValuesAlignedDataMaxLength"
#define OCPP_CONFIG_KEY_METER_VALUES_SAMPLED_DATA "MeterValuesSampledData"
#define OCPP_CONFIG_KEY_METER_VALUES_SAMPLED_DATA_MAX_LENGTH "MeterValuesSampledDataMaxLength"
#define OCPP_CONFIG_KEY_METER_VALUE_SAMPLE_INTERVAL "MeterValueSampleInterval"
#define OCPP_CONFIG_KEY_NUMBER_OF_CONNECTORS "NumberOfConnectors"
#define OCPP_CONFIG_KEY_RESET_RETRIES "ResetRetries"
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
// local auth list management profile
#define OCPP_CONFIG_KEY_LOCAL_AUTH_LIST_ENABLED "LocalAuthListEnabled"
#define OCPP_CONFIG_KEY_LOCAL_AUTH_LIST_MAX_LENGTH "LocalAuthListMaxLength"
#define OCPP_CONFIG_KEY_SEND_LOCAL_LIST_MAX_LENGTH "SendLocalListMaxLength"
///@}

/// The number of unique config keys
#define OCPP_CONFIG_KEY_COUNT 31

/**
 * @brief Contains information about a specific configuration key. It is returned in GetConfiguration.conf.
 */
struct ocpp_key_value{
	char key[34]; ///< "Required" NOTE: the specification uses CiString50Type but longest core is 33 and longest read only is 39
	bool readonly; ///< "Required. False if the value can be set with the ChangeConfiguration message"
	char * value; ///< "Optional. If key is known but not set, this field may be absent"
};

/**
 * @brief converts key value to JSON equivalent
 *
 * @param key_value value to convert
 */
cJSON * create_key_value_json(struct ocpp_key_value key_value);

/**
 * @brief check if given key is a valid key
 *
 * @param key value to check if valid.
 */
bool is_configuration_key(const char * key);

#endif /*OCPP_KEY_VALUE_H*/
