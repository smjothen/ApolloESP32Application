#include "segmented_ota.h"
#include "esp_ota_ops.h"

#include "stdio.h"
#include "string.h"
#include "freertos/task.h"

#include "ota_log.h"
#include "esp_log.h"

static const char *TAG = "segmented_ota";

int total_size = 0;
esp_ota_handle_t update_handle = { 0 };


extern const uint8_t server_cert_pem_start[] asm("_binary_ca_cert_pem_start");

static esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{

    int * error_p  = (int * )evt->user_data;

    switch (evt->event_id) {
    case HTTP_EVENT_ERROR:
        ESP_LOGW(TAG, "HTTP_EVENT_ERROR");
        *error_p = -1;
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
        esp_err_t err = esp_ota_write(update_handle, evt->data, evt->data_len);
        if(err!=ESP_OK){
            ESP_LOGE(TAG, "Writing data to flash failed");
            *error_p = 3;
            ota_log_chunk_flash_error(err);
        }

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
    ota_log_chunked_update_start(image_location);

    esp_err_t err = esp_ota_begin(
        esp_ota_get_next_update_partition(NULL),
         OTA_SIZE_UNKNOWN, &update_handle
    );
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed (%s)", esp_err_to_name(err));
        return;
    }

    int read_start=0;
    int chunk_size = 65536;
    int read_end = read_start + chunk_size -1; // inclusive read end
    
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
            ESP_LOGW(TAG, "Flashing all segments done, proceeding to validation");
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
        }else{
            ota_log_chunk_http_error(err);
        }
        esp_http_client_cleanup(client);

        if(flash_error != 0){
            ESP_LOGW(TAG, "error when flashing segment, retrying");
            continue;
        }

        ESP_LOGI(TAG, "Flashed %d tot %d of %d", read_start, read_end, total_size);
        ota_log_chunk_flashed(read_start, read_end, total_size);

        read_start = read_end + 1;
        read_end = read_start + chunk_size -1;

        if((total_size > 0 )&&(read_end>total_size)){
            read_end = total_size;
        }
    }
    
    esp_err_t end_err = esp_ota_end(update_handle);
    if(end_err!=ESP_OK){
        ESP_LOGE(TAG, "Partition validation error %d", end_err);
        ota_log_chunk_validation_error(end_err);
    }else{
        ESP_LOGW(TAG, "update complete, rebooting soon");
        ota_log_all_chunks_success();
    }
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();


}
