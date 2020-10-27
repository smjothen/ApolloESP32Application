#include "ota_location.h"
#include <string.h>
// #include "esp_tls.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "cJSON.h"

#define TAG "OTA_LOCATION"

#define MAX_HTTP_RECV_BUFFER 1536//512
//#define MAX_HTTP_OUTPUT_BUFFER 2048

extern const uint8_t server_cert_pem_start[] asm("_binary_ca_cert_pem_start");

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
            int mbedtls_err = 0;
            esp_err_t err = 0;
            // esp_err_t err = esp_tls_get_and_clear_last_error(evt->data, &mbedtls_err, NULL);
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
    }
    return ESP_OK;
}


int get_image_location(char * location, int buffersize){
    ESP_LOGI(TAG, "getting ota image location");

    char local_response_buffer[MAX_HTTP_RECV_BUFFER] = {0};
    esp_http_client_config_t config = {
    	.url = "https://api.zaptec.com/api/firmware/ZAP000018/current",
        //.host = "httpbin.org",
        //.path = "/get",
        //.query = "esp",
        .event_handler = _http_event_handler,
        .user_data = local_response_buffer,
        .cert_pem = (char *)server_cert_pem_start,
		.timeout_ms = 20000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    // POST
    //const char *post_data = "{\"psk\":\"ubTCXZJoEs8LjFw3lVFzSLXQ0CCJDEiNt7AyqbvxwFA=\"}";
    const char *post_data = "{\"psk\":\"NusI1QY66Hfnag1TE97gDmCepQVlD+4aYBZjRztzDIs=\"}";
    //const char *url = "https://api.zaptec.com/api/firmware/ZAP000018/current";
    //esp_http_client_set_url(client, "https://api.zaptec.com/api/firmware/ZAP000018/current");
    //esp_http_client_set_url(client, url);
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP POST Status = %d, content_length = %d",
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
            //free(body);
            cJSON_Delete(body);
        }else{
            ESP_LOGW(TAG, "bad body");
        }
    } else {
        ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
    }



    return 0;
}
