#include "segmented_ota.h"
#include "esp_ota_ops.h"

#include "stdio.h"
#include "string.h"
#include "freertos/task.h"

#include "ota_log.h"
#include "esp_log.h"
#include "certificate.h"
#include <math.h>

#include "zaptec_cloud_observations.h"

static const char *TAG = "safe_ota";

static int total_size = 0;
static esp_ota_handle_t update_handle = { 0 };
static unsigned char * blockBuffer = NULL;
static int bufLength = 0;

static int sentBlockCnt = 0;
static int recvBlockCnt = 0;

//extern const uint8_t server_cert_pem_start[] asm("_binary_ca_cert_pem_start");

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
        //ESP_LOGI(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
        if(strcmp(evt->header_key, "X-Total-Content-Length")==0){
            sscanf(evt->header_value, "%d", &total_size);
        }
        break;
    case HTTP_EVENT_ON_DATA:
        memcpy(&blockBuffer[bufLength], evt->data, evt->data_len);
        bufLength += evt->data_len;
        //ESP_LOGI(TAG, "got ota data to buffered, %d bytes, %d tot", evt->data_len, bufLength);
        break;
    case HTTP_EVENT_ON_FINISH:

        ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH - SAVING BLOCK LENGTH %d", bufLength);
        /*esp_err_t err = esp_ota_write(update_handle, blockBuffer, bufLength);
		if(err!=ESP_OK){
			ESP_LOGE(TAG, "Writing data to flash failed");
			*error_p = 3;
			ota_log_chunk_flash_error(err);
		}*/

        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
        break;
    case HTTP_EVENT_REDIRECT:
        ESP_LOGI(TAG, "HTTP_EVENT_REDIRECT");
        break;
    }
    return ESP_OK;
}

static bool doAbortOTA = false;
void do_safe_ota_abort()
{
	doAbortOTA = true;
}


static int chunk_size = 65536;
void ota_set_chunk_size(int newSize)
{
	chunk_size = newSize;
	ESP_LOGW(TAG, "New chuck size: %d", chunk_size);
}

void do_safe_ota(char *image_location){
    ESP_LOGW(TAG, "running experimental safe ota");
    ota_log_chunked_update_start(image_location);

    const esp_partition_t * update_partition = esp_ota_get_next_update_partition(NULL);

    esp_err_t err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed (%s)", esp_err_to_name(err));
        return;
    }

    int read_start=0;
    //int chunk_size = 65536;
    int read_end = read_start + chunk_size -1; // inclusive read end
    
    int flash_error = 0;

    bool useCert = certificate_GetUsage();

    if(!useCert)
    	ESP_LOGE(TAG, "CERTIFICATES NOT USED");

    int bufferSize = 1536;

    esp_http_client_config_t config = {
        .url = image_location,
        //.cert_pem = (char *)server_cert_pem_start,
        .use_global_ca_store = useCert,
		//.transport_type = HTTP_TRANSPORT_OVER_SSL,
        .event_handler = _http_event_handler,
		.timeout_ms = 20000,
		.buffer_size = bufferSize,
        .user_data = &flash_error,
    };

    blockBuffer = malloc(chunk_size);
    recvBlockCnt = 0;
    sentBlockCnt = 0;
    int nrOfBlocks = 0;


    while(true){
        if((total_size > 0 )&&(read_start>total_size)){
            ESP_LOGW(TAG, "Flashing all segments done, proceeding to validation");
            break;
        }

        if(doAbortOTA == true)
        	break;

        bufLength = 0;
        memset(blockBuffer, 0, chunk_size);

        if((nrOfBlocks == 0) && (total_size > 0))
        {
        	nrOfBlocks = ceil(total_size/(chunk_size * 1.0));
        	ESP_LOGW(TAG, "nrOfBlocks: %d",nrOfBlocks);
        }

        esp_http_client_handle_t client = esp_http_client_init(&config);

        char range_header_value[64];
        snprintf(range_header_value, 64, "bytes=%d-%d", read_start, read_end);
        esp_http_client_set_header(client, "Range", range_header_value);

        ESP_LOGI(TAG, "fetching [%s] (blksize %d)", range_header_value, chunk_size);

        //sentBlockCnt++;
        //ESP_LOGW(TAG, "Sending block #%d", sentBlockCnt);

        esp_err_t err = esp_http_client_perform(client);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Status = %d, content_length = %" PRId64 "",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));

            recvBlockCnt++;

            int expectedBlockLength = read_end - read_start +1;
            ESP_LOGW(TAG, "expectedBlockLength = %d -> %d = %d(%d)", read_start, read_end, expectedBlockLength, bufLength);

	    	if(((recvBlockCnt < nrOfBlocks) && (bufLength != expectedBlockLength)) || ((recvBlockCnt == nrOfBlocks) && (bufLength != (expectedBlockLength-1))))
	    	{
	    		//Go back on block
	    		recvBlockCnt--;

	    		/// Do not write anything - retry same block
	    		ESP_LOGE(TAG, "Invalid block length #%d/%d length: %d", recvBlockCnt, nrOfBlocks, bufLength);
				ota_log_chunk_http_error(err);
				esp_http_client_cleanup(client);
				vTaskDelay(pdMS_TO_TICKS(3000));
				continue;
	    	}

	    	ESP_LOGW(TAG, "Received block #%d/%d Start: %d", recvBlockCnt, nrOfBlocks, read_start);

            esp_err_t errf = esp_ota_write(update_handle, blockBuffer, bufLength);
			if(errf!=ESP_OK){
				ESP_LOGE(TAG, "Writing data to flash failed");
				ota_log_chunk_flash_error(errf);
			}

        }else{
        	ESP_LOGE(TAG, "HTTP ERROR %d. RETRYING SAME BLOCK Start: %d", err, read_start);
        	/// Do not write anything - retry same block
            ota_log_chunk_http_error(err);
            esp_http_client_cleanup(client);
            vTaskDelay(pdMS_TO_TICKS(3000));
            continue;
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
    
    free(blockBuffer);

    if(doAbortOTA == true)
    {
    	doAbortOTA = false;
    	ESP_LOGW(TAG, "Aborting OTA");
    	return;
    }

    esp_err_t end_err = esp_ota_end(update_handle);
    if(end_err!=ESP_OK){
        ESP_LOGE(TAG, "Partition validation error %d", end_err);
        ota_log_chunk_validation_error(end_err);
        publish_debug_telemetry_security_log("OTA", "Rejected");
    }else{
        ESP_LOGW(TAG, "update complete, rebooting soon");
        ota_log_all_chunks_success();
        publish_debug_telemetry_security_log("OTA", "Accepted");
    }
    publish_debug_telemetry_observation_capabilities_clear();

    vTaskDelay(pdMS_TO_TICKS(3000));
    end_err = esp_ota_set_boot_partition(update_partition);
    if(end_err!=ESP_OK)
    	ESP_LOGE(TAG, "Set boot partition error %d", end_err);

    esp_restart();
}
