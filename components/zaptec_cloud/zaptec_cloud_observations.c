#include "cJSON.h"
#include "esp_log.h"
#include "time.h"
#include <sys/time.h>
#include "stdio.h"

#include "zaptec_cloud_listener.h"
#include "zaptec_cloud_observations.h"

#define TAG "OBSERVATIONS POSTER"

int publish_json(cJSON *payload){
    char *message = cJSON_PrintUnformatted(payload);
    ESP_LOGI(TAG, "<<<sending>>> %s", message);

    int publish_err = publish_iothub_event(message);

    cJSON_free(payload);
    free(message);

    if(publish_err){
        ESP_LOGW(TAG, "publish to iothub failed");
        return -1;
    }
    return 0;
}

cJSON *create_observation(int observation_id, char *value){
    cJSON *result = cJSON_CreateObject();
    if(result == NULL){return NULL;}

    struct timeval tv_now;
    gettimeofday(&tv_now, NULL);

    time_t now = tv_now.tv_sec;
    struct tm timeinfo = { 0 };
    char strftime_buf_head[64];
    char strftime_buf[128];
    time(&now);
    setenv("TZ", "UTC-0", 1);
    tzset();
    localtime_r(&now, &timeinfo);
    strftime(strftime_buf_head, sizeof(strftime_buf_head), "%Y-%m-%dT%H:%M:%S.", &timeinfo);
    snprintf(strftime_buf, sizeof(strftime_buf), "%s%03dZ", strftime_buf_head, (int)(tv_now.tv_usec/1000));
    
    cJSON_AddStringToObject(result, "ObservedAt", strftime_buf);
    cJSON_AddStringToObject(result, "Value", value);
    cJSON_AddNumberToObject(result, "ObservationId", (float) observation_id);
    cJSON_AddNumberToObject(result, "Type", (float) 1.0);

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
    double voltage_l1, double voltage_l2, double voltage_l3,
    double current_l1, double current_l2, double current_l3,
    double temperature_5, double temperature_emeter
){
    ESP_LOGD(TAG, "sending charging telemetry");

    cJSON *observations = create_observation_collection();
    add_observation_to_collection(observations, create_observation(808, "debugstring1"));
    add_observation_to_collection(observations, create_observation(808, "debugstring2"));

    add_observation_to_collection(observations, create_double_observation(501, voltage_l1));
    add_observation_to_collection(observations, create_double_observation(502, voltage_l2));
    add_observation_to_collection(observations, create_double_observation(503, voltage_l3));

    add_observation_to_collection(observations, create_double_observation(507, current_l1));
    add_observation_to_collection(observations, create_double_observation(508, current_l2));
    add_observation_to_collection(observations, create_double_observation(509, current_l2));

    add_observation_to_collection(observations, create_double_observation(201, temperature_5));
    add_observation_to_collection(observations, create_double_observation(202, temperature_emeter));

    return publish_json(observations);
}

int publish_diagnostics_observation(char *message){
    return publish_json(create_observation(808, message));
}

int publish_debug_message_event(char *message, cloud_event_level level){

    cJSON *event = cJSON_CreateObject();
    if(event == NULL){return -10;}

    cJSON_AddNumberToObject(event, "EventType", level);
    cJSON_AddStringToObject(event, "Message", message);
    cJSON_AddNumberToObject(event, "Type", (float) 5.0);

    return publish_json(event);
}