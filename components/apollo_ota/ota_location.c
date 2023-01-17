#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ota_location.h"
#include <string.h>
#include "esp_tls.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "cJSON.h"
#include "i2cDevices.h"
#include "string.h"
#include "certificate.h"
#include "../../main/DeviceInfo.h"


#define TAG "OTA_LOCATION"

#define MAX_HTTP_RECV_BUFFER 1536//512
//#define MAX_HTTP_OUTPUT_BUFFER 2048

//extern const uint8_t server_cert_pem_start[] asm("_binary_ca_cert_pem_start");
static int mbedtls_err = 0;

static esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    static char *output_buffer;  // Buffer to store response of http request from event handler
    static int output_len;       // Stores number of bytes read

    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            /*
             *  Check for chunked encoding is added as the URL for chunked encoding used in this example returns binary data.
             *  However, event handler can also be used in case chunked encoding is used.
             */
            if (!esp_http_client_is_chunked_response(evt->client)) {
                // If user_data buffer is configured, copy the response into the buffer
                if (evt->user_data) {
                    memcpy(evt->user_data + output_len, evt->data, evt->data_len);
                } else {
                    if (output_buffer == NULL) {
                        output_buffer = (char *) malloc(esp_http_client_get_content_length(evt->client));
                        output_len = 0;
                        if (output_buffer == NULL) {
                            ESP_LOGE(TAG, "Failed to allocate memory for output buffer");
                            return ESP_FAIL;
                        }
                    }
                    memcpy(output_buffer + output_len, evt->data, evt->data_len);
                }
                output_len += evt->data_len;
            }

            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            if (output_buffer != NULL) {
                // Response is accumulated in output_buffer. Uncomment the below line to print the accumulated response
                ESP_LOG_BUFFER_HEX(TAG, output_buffer, output_len);
                free(output_buffer);
                output_buffer = NULL;
            }
            output_len = 0;
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");

            //esp_err_t err = 0;
            esp_err_t err = esp_tls_get_and_clear_last_error(evt->data, &mbedtls_err, NULL);
            if (err != 0) {
                if (output_buffer != NULL) {
                    free(output_buffer);
                    output_buffer = NULL;
                }
                output_len = 0;
                ESP_LOGI(TAG, "Last esp error code: 0x%x", err);
                ESP_LOGI(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
            }
            break;
        case HTTP_EVENT_REDIRECT:
            ESP_LOGI(TAG, "HTTP_EVENT_REDIRECT");
            break;
    }
    return ESP_OK;
}

#include "esp_heap_task_info.h"

#define MAX_TASK_NUM 30                         // Max number of per tasks info that it can store
#define MAX_BLOCK_NUM 30                        // Max number of per block info that it can store

static size_t s_prepopulated_num = 0;
static heap_task_totals_t s_totals_arr[MAX_TASK_NUM];
static heap_task_block_t s_block_arr[MAX_BLOCK_NUM];

static void esp_dump_per_task_heap_info(void)
{
    heap_task_info_params_t heap_info = {0};
    heap_info.caps[0] = MALLOC_CAP_8BIT;        // Gets heap with CAP_8BIT capabilities
    heap_info.mask[0] = MALLOC_CAP_8BIT;
    heap_info.caps[1] = MALLOC_CAP_32BIT;       // Gets heap info with CAP_32BIT capabilities
    heap_info.mask[1] = MALLOC_CAP_32BIT;
    heap_info.tasks = NULL;                     // Passing NULL captures heap info for all tasks
    heap_info.num_tasks = 0;
    heap_info.totals = s_totals_arr;            // Gets task wise allocation details
    heap_info.num_totals = &s_prepopulated_num;
    heap_info.max_totals = MAX_TASK_NUM;        // Maximum length of "s_totals_arr"
    heap_info.blocks = s_block_arr;             // Gets block wise allocation details. For each block, gets owner task, address and size
    heap_info.max_blocks = MAX_BLOCK_NUM;       // Maximum length of "s_block_arr"

    heap_caps_get_per_task_info(&heap_info);

    for (int i = 0 ; i < *heap_info.num_totals; i++) {
        printf("Task: %s -> CAP_8BIT: %d CAP_32BIT: %d\n",
                heap_info.totals[i].task ? pcTaskGetTaskName(heap_info.totals[i].task) : "Pre-Scheduler allocs" ,
                heap_info.totals[i].size[0],    // Heap size with CAP_8BIT capabilities
                heap_info.totals[i].size[1]);   // Heap size with CAP32_BIT capabilities
    }

    printf("\n\n");
}

static void log_task_info(void)
{
    char task_info[40 * 15];

    // https://www.freertos.org/a00021.html#vTaskList
    vTaskList(task_info);
    ESP_LOGD(TAG, "[vTaskList:]\n\r"
                  "name\t\tstate\tpri\tstack\tnum\tcoreid"
                  "\n\r%s\n",
             task_info);

    vTaskGetRunTimeStats(task_info);
    ESP_LOGD(TAG, "[vTaskGetRunTimeStats:]\n\r"
                  "\rname\t\tabsT\t\trelT\trelT"
                  "\n\r%s\n",
             task_info);

    size_t total_size = heap_caps_get_total_size(MALLOC_CAP_8BIT);
    ESP_LOGI(TAG, "available heap size: %d", total_size);

    // memory info as extracted in the HAN adapter project:
    size_t free_heap_size = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);

    // https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/heap_debug.html
    char formated_memory_use[256];
    snprintf(formated_memory_use, 256,
             "[MEMORY USE] (GetFreeHeapSize now: %" PRId32 ", GetMinimumEverFreeHeapSize: %" PRId32 ", heap_caps_get_free_size: %d)",
             xPortGetFreeHeapSize(), xPortGetMinimumEverFreeHeapSize(), free_heap_size);
    ESP_LOGD(TAG, "freertos api result:\n\r%s", formated_memory_use);

    // heap_caps_print_heap_info(MALLOC_CAP_EXEC|MALLOC_CAP_32BIT|MALLOC_CAP_8BIT|MALLOC_CAP_INTERNAL|MALLOC_CAP_DEFAULT|MALLOC_CAP_IRAM_8BIT);
    heap_caps_print_heap_info(MALLOC_CAP_INTERNAL);

    ESP_LOGD(TAG, "\n---------------------------\n");
    heap_caps_print_heap_info(MALLOC_CAP_8BIT);

    ESP_LOGD(TAG, "log_task_info done");
}

int get_image_location(char *location, int buffersize, char * version)
{
    char url [150];
#ifdef DEVELOPEMENT_URL
    snprintf(url, 150, "https://dev-api.zaptec.com/api/firmware/%.10s/current", i2cGetLoadedDeviceInfo().serialNumber);
    ESP_LOGE(TAG, "###### USING DEVELOPMENT URL !!! #######");
#else
    snprintf(url, 150, "https://api.zaptec.com/api/firmware/%.10s/current", i2cGetLoadedDeviceInfo().serialNumber);
#endif

    ESP_LOGI(TAG, "getting ota image location from %s", url);

    bool useCert = certificate_GetUsage();

	if(!useCert)
		ESP_LOGE(TAG, "CERTIFICATES NOT USED");


    char local_response_buffer[MAX_HTTP_RECV_BUFFER] = {0};
    esp_http_client_config_t config = {
    	.url = url,
        //.host = "httpbin.org",
        //.path = "/get",
        //.query = "esp",
        .event_handler = _http_event_handler,
        .user_data = local_response_buffer,
		.use_global_ca_store = useCert,
        //.cert_pem = (char *)server_cert_pem_start,
		//.transport_type = HTTP_TRANSPORT_OVER_SSL,
		.timeout_ms = 20000,
		.buffer_size = 1536,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    // POST
    //const char *post_data = "{\"psk\":\"ubTCXZJoEs8LjFw3lVFzSLXQ0CCJDEiNt7AyqbvxwFA=\"}";
    char post_data [61];
    snprintf(post_data, 61,"{\"psk\":\"%s\"}", i2cGetLoadedDeviceInfo().PSK );
    //const char *url = "https://api.zaptec.com/api/firmware/ZAP000018/current";
    //esp_http_client_set_url(client, "https://api.zaptec.com/api/firmware/ZAP000018/current");
    //esp_http_client_set_url(client, url);
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_dump_per_task_heap_info();
    log_task_info();

    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP POST Status = %d, content_length = %" PRId64 "",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
        ESP_LOGI(TAG, "Body: %s", local_response_buffer);
        cJSON *body = cJSON_Parse(local_response_buffer);
        if(body!=NULL){
            if(cJSON_HasObjectItem(body, "DownloadUrl")){
                strncpy(
                    location, 
                    cJSON_GetObjectItem(body, "DownloadUrl")->valuestring,
                    buffersize);
            }else{
                ESP_LOGW(TAG, "bad json");
            }
            if(cJSON_HasObjectItem(body, "Version")){
				strncpy(
					version,
					cJSON_GetObjectItem(body, "Version")->valuestring,
					strlen(cJSON_GetObjectItem(body, "Version")->valuestring));
			}else{
				ESP_LOGW(TAG, "bad json");
			}
            cJSON_Delete(body);
        }else{
            ESP_LOGW(TAG, "bad body");
        }
    } else {
        ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
        ESP_LOGI(TAG, "HTTP POST Status = %d, content_length = %" PRId64 "",
                        esp_http_client_get_status_code(client),
                        esp_http_client_get_content_length(client));
                ESP_LOGI(TAG, "Body: %s", local_response_buffer);
    }

    //If certificate has expired, flag return the error code
    int ret = 0;

    esp_http_client_cleanup(client);
    log_task_info();

    if(mbedtls_err == 0x2700)
	{
		ret = mbedtls_err;
	}

    return ret;
}
