#include "segmented_ota.h"

#include "stdio.h"
#include "string.h"

#include "ota_log.h"
#include "esp_log.h"

static const char *TAG = "segmented_ota";

int total_size = 0;

extern const uint8_t server_cert_pem_start[] asm("_binary_ca_cert_pem_start");

static esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ERROR:
        ESP_LOGW(TAG, "HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED, setting debug header");
        esp_http_client_set_header(evt->client, "Zaptec-Debug-Info", "apollo/ota/arnt/1");
        break;
    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGI(TAG, "HTTP_EVENT_HEADER_SENT");
        break;
    case HTTP_EVENT_ON_HEADER:
        ESP_LOGI(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
        if(strcmp(evt->header_key, "X-Total-Content-Length")==0){
            sscanf(evt->header_value, "%d", &total_size);
        }
        break;
    case HTTP_EVENT_ON_DATA:
        ESP_LOGI(TAG, "got ota data, %d bytes", evt->data_len);
        int * error_p  = (int * )evt->user_data;
        *error_p = 3;
        ota_log_download_progress_debounced(evt->data_len);
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH");
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
        break;
    }
    return ESP_OK;
}

void do_segmented_ota(char *image_location){
    ESP_LOGW(TAG, "running experimental segmented ota");
    esp_log_level_set(TAG, ESP_LOG_DEBUG);

    int read_start=0;
    int chunk_size = 65536;
    int read_end = read_start + chunk_size;
    
    int flash_error = 0;

    esp_http_client_config_t config = {
        .url = image_location,
        .cert_pem = (char *)server_cert_pem_start,
        // .use_global_ca_store = true,
        .event_handler = _http_event_handler,
		.timeout_ms = 20000,
		.buffer_size = 1536,
        .user_data = &flash_error,
    };

    while(true){
        if((total_size > 0 )&&(read_start>total_size)){
            ESP_LOGW(TAG, "Flashing all segments done");
            break;
        }

        esp_http_client_handle_t client = esp_http_client_init(&config);

        char range_header_value[64];
        snprintf(range_header_value, 64, "bytes=%d-%d", read_start, read_end);
        esp_http_client_set_header(client, "Range", range_header_value);

        ESP_LOGI(TAG, "fetching [%s]", range_header_value);
        esp_err_t err = esp_http_client_perform(client);
        if (err == ESP_OK) {
        ESP_LOGI(TAG, "Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
        }
        esp_http_client_cleanup(client);

        //TODO: if error continue;
        if(flash_error > 0){
            ESP_LOGW(TAG, "error when flashing segment, retrying");
            //continue;
        }

        read_start = read_end + 1;
        read_end = read_start + chunk_size;

        if((total_size > 0 )&&(read_end>total_size)){
            read_end = total_size;
        }
    }
    
    ESP_LOGW(TAG, "TODO mark partition");


}
