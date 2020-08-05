#include "cJSON.h"
#include "esp_log.h"
#include "time.h"
#include "stdio.h"

#include "zaptec_cloud_listener.h"
#include "zaptec_cloud_observations.h"

#define TAG "OBSERVATIONS POSTER"

int publish_json(cJSON *payload){
    char *message = cJSON_PrintUnformatted(payload);
    ESP_LOGI(TAG, "sending %s", message);

    int publish_err = publish_iothub_event(message);

    //cJSON_free(payload);
    free(message);

    if(publish_err){
        return -1;
    }
    return 0;
}

cJSON *create_observation(int observation_id, char *value){
    cJSON *result = cJSON_CreateObject();
    if(result == NULL){return NULL;}

    time_t now = 0;
    struct tm timeinfo = { 0 };
    char strftime_buf[64];
    time(&now);
    setenv("TZ", "UTC-0", 1);
    tzset();
    localtime_r(&now, &timeinfo);
    strftime(strftime_buf, sizeof(strftime_buf), "%Y-%m-%dT%H:%M:%S.000Z", &timeinfo);
    
    cJSON_AddStringToObject(result, "ObservedAt", strftime_buf);
    cJSON_AddStringToObject(result, "Value", value);
    cJSON_AddNumberToObject(result, "ObservationId", (float) observation_id);
    cJSON_AddNumberToObject(result, "Type", (float) 2.0);

    return result;
}

cJSON *create_double_observation(int observation_id, double value){
    char value_string[32];
    sprintf(value_string, "%f", value);
    return create_observation(observation_id, value_string);
}

cJSON *create_observation_collection(void){
    cJSON *result = cJSON_CreateObject();
    if(result == NULL){return NULL;}

    cJSON_AddArrayToObject(result, "Observations");
    cJSON_AddNumberToObject(result, "Type", (float) 6.0);
    return result;
}

int add_observation_to_collection(cJSON *collection, cJSON *observation){
    cJSON_AddItemToArray(
        cJSON_GetObjectItem(collection, "Observations"),
        observation
    );

    return 0;
}

int publish_debug_telemetry_observation(
    double temperature_5, double temperature_emeter, double rssi
){
    ESP_LOGD(TAG, "sending charging telemetry");

    cJSON *observations = create_observation_collection();
    add_observation_to_collection(observations, create_observation(911, "0.0.0.1"));
    add_observation_to_collection(observations, create_observation(808, "debugstring1"));

    /*add_observation_to_collection(observations, create_double_observation(501, voltage_l1));
    add_observation_to_collection(observations, create_double_observation(502, voltage_l2));
    add_observation_to_collection(observations, create_double_observation(503, voltage_l3));

    add_observation_to_collection(observations, create_double_observation(507, current_l1));
    add_observation_to_collection(observations, create_double_observation(508, current_l2));
    add_observation_to_collection(observations, create_double_observation(509, current_l2));*/

    add_observation_to_collection(observations, create_double_observation(201, temperature_5));
    add_observation_to_collection(observations, create_double_observation(809, rssi));
    //add_observation_to_collection(observations, create_double_observation(202, temperature_emeter));

    return publish_json(observations);
}

int publish_debug_telemetry_observation_power(
    double voltage_l1, double voltage_l2, double voltage_l3,
    double current_l1, double current_l2, double current_l3
){
    ESP_LOGD(TAG, "sending charging telemetry");

    cJSON *observations = create_observation_collection();

    add_observation_to_collection(observations, create_double_observation(501, voltage_l1));
    add_observation_to_collection(observations, create_double_observation(502, voltage_l2));
    add_observation_to_collection(observations, create_double_observation(503, voltage_l3));

    add_observation_to_collection(observations, create_double_observation(507, current_l1));
    add_observation_to_collection(observations, create_double_observation(508, current_l2));
    add_observation_to_collection(observations, create_double_observation(509, current_l2));

    return publish_json(observations);
}


int publish_debug_telemetry_observation_all(
	double temperature_emeter1, double temperature_emeter2, double temperature_emeter3,
	double temperature_TM, double temperature_TM2,
    double voltage_l1, double voltage_l2, double voltage_l3,
    double current_l1, double current_l2, double current_l3,
	double rssi
){
    ESP_LOGD(TAG, "sending charging telemetry");

    cJSON *observations = create_observation_collection();

    add_observation_to_collection(observations, create_observation(911, "0.0.0.1"));
    //add_observation_to_collection(observations, create_observation(808, "debugstring1"));

    add_observation_to_collection(observations, create_double_observation(202, temperature_emeter1));
    add_observation_to_collection(observations, create_double_observation(203, temperature_emeter2));
    add_observation_to_collection(observations, create_double_observation(204, temperature_emeter3));
    add_observation_to_collection(observations, create_double_observation(205, temperature_TM));
    add_observation_to_collection(observations, create_double_observation(205, temperature_TM2));

    add_observation_to_collection(observations, create_double_observation(501, voltage_l1));
    add_observation_to_collection(observations, create_double_observation(502, voltage_l2));
    add_observation_to_collection(observations, create_double_observation(503, voltage_l3));

    add_observation_to_collection(observations, create_double_observation(507, current_l1));
    add_observation_to_collection(observations, create_double_observation(508, current_l2));
    add_observation_to_collection(observations, create_double_observation(509, current_l3));

	add_observation_to_collection(observations, create_double_observation(809, rssi));
	int ret = publish_json(observations);

	cJSON_Delete(observations);

    return ret;//publish_json(observations);
}


int publish_debug_message_event(char *message, cloud_event_level level){

    cJSON *event = cJSON_CreateObject();
    if(event == NULL){return NULL;}

    cJSON_AddNumberToObject(event, "EventType", level);
    cJSON_AddStringToObject(event, "Message", message);
    cJSON_AddNumberToObject(event, "Type", (float) 5.0);

    return publish_json(event);
}

int publish_cloud_pulse(void){
    cJSON *pulse = cJSON_CreateObject();
    if(pulse == NULL){return -10;}

    cJSON_AddNumberToObject(pulse, "Type", (float) 2.0);

    ESP_LOGD(TAG, "sending pulse to cloud");
    return publish_json(pulse);
}
