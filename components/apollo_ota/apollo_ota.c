#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include <string.h>
#include "freertos/event_groups.h"

#include "apollo_ota.h"
#include "ota_location.h"

#include "zaptec_protocol_serialisation.h"
#include "protocol_task.h"
#include "mcu_communication.h"

#define TAG "OTA"

extern const uint8_t server_cert_pem_start[] asm("_binary_ca_cert_pem_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_ca_cert_pem_end");

extern const uint8_t dspic_bin_start[] asm("_binary_dspic_bin_start");
extern const uint8_t dspic_bin_end[] asm("_binary_dspic_bin_end");

static EventGroupHandle_t event_group;
static const int OTA_UNBLOCKED = BIT0;

//from https://rosettacode.org/wiki/CRC-32#C
uint32_t
rc_crc32(uint32_t crc, const char *buf, size_t len)
{
	static uint32_t table[256];
	static int have_table = 0;
	uint32_t rem;
	uint8_t octet;
	int i, j;
	const char *p, *q;
 
	/* This check is not thread safe; there is no mutex. */
	if (have_table == 0) {
		/* Calculate CRC table. */
		for (i = 0; i < 256; i++) {
			rem = i;  /* remainder from polynomial division */
			for (j = 0; j < 8; j++) {
				if (rem & 1) {
					rem >>= 1;
					rem ^= 0xedb88320;
				} else
					rem >>= 1;
			}
			table[i] = rem;
		}
		have_table = 1;
	}
 
	crc = ~crc;
	q = buf + len;
	for (p = buf; p < q; p++) {
		octet = *p;  /* Cast to unsigned octet. */
		crc = (crc >> 8) ^ table[(crc & 0xff) ^ octet];
	}
	return ~crc;
}

static esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
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
        // ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len); to much noice
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
        break;
    }
    return ESP_OK;
}


static void ota_task(void *pvParameters){
    char image_location[256] = {0};
    esp_http_client_config_t config = {
        .url = image_location,
        .cert_pem = (char *)server_cert_pem_start,
        // .use_global_ca_store = true,
        .event_handler = _http_event_handler,
    };

    // config.skip_cert_common_name_check = true;

    while (true)
    {
        ESP_LOGI(TAG, "waiting for ota event");
        xEventGroupWaitBits(event_group, OTA_UNBLOCKED, pdFALSE, pdFALSE, portMAX_DELAY);
        ESP_LOGW(TAG, "attempting ota update");

        get_image_location(image_location,sizeof(image_location));
        // strcpy( image_location,"http://api.zaptec.com/api/firmware/6476103f-7ef9-4600-9450-e72a282c192b/download");
        // strcpy( image_location,"https://api.zaptec.com/api/firmware/ZAP000001/current");
        ESP_LOGI(TAG, "image location to use: %s", image_location);

        esp_err_t ret = esp_https_ota(&config);
        if (ret == ESP_OK) {
            esp_restart();
        } else {
            ESP_LOGE(TAG, "Firmware upgrade failed");
        }

        vTaskDelay(3000 / portTICK_RATE_MS);
    }
}

void validate_booted_image(void){
    const esp_partition_t * partition = esp_ota_get_running_partition();
    ESP_LOGI(TAG, "Checking if VALID on partition %s ", partition->label);

    esp_ota_img_states_t ota_state;
    esp_err_t ret = esp_ota_get_state_partition(partition, &ota_state);

    if(ota_state == ESP_OTA_IMG_PENDING_VERIFY)
    {
        ret = esp_ota_mark_app_valid_cancel_rollback();
        if(ret != ESP_OK){
             ESP_LOGE(TAG, "marking partition as valid failed with: %d", ret);
        }else{
            ESP_LOGI(TAG, "partition marked as valid");
        }
    }
    else
    {
        ESP_LOGI(TAG, "partition already valid");
    }
}


#define COMMAND_NACK        0x00
#define COMMAND_ACK         0x01
#define COMMAND_READ_PM     0x02
#define COMMAND_WRITE_PM    0x03
#define COMMAND_WRITE_CM    0x07
#define COMMAND_START_APP   0x08
#define COMMAND_START_BL    0x18
#define COMMAND_READ_ID     0x09
#define COMMAND_APP_CRC     0x0A

void ensure_dspic_updated(void){
    ESP_LOGI(TAG, "checking dsPIC FW version");
    while (true)
    {
    
        ESP_LOGI(TAG, "creating zap message");
        ZapMessage txMsg;

        // ZEncodeMessageHeader* does not check the length of the buffer!
        // This should not be a problem for most usages, but make sure strings are within a range that fits!
        uint8_t txBuf[ZAP_PROTOCOL_BUFFER_SIZE];
        uint8_t encodedTxBuf[ZAP_PROTOCOL_BUFFER_SIZE_ENCODED];
        
        txMsg.type = MsgFirmware;
        txMsg.identifier = ParamRunTest;

        uint encoded_length = ZEncodeMessageHeaderAndOneByte(
            &txMsg, COMMAND_START_APP, txBuf, encodedTxBuf
        );

        ESP_LOGI(TAG, "sending zap message, %d bytes", encoded_length);
        
        ZapMessage rxMsg = runRequest(encodedTxBuf, encoded_length);
        printf("frame type: %d \n\r", rxMsg.type);
        printf("frame identifier: %d \n\r", rxMsg.identifier);
        printf("frame timeId: %d \n\r", rxMsg.timeId);

        // uint8_t error_code = ZDecodeUInt8(rxMsg.data);
        // printf("frame error code: %d\n\r", error_code);
        freeZapMessageReply();
        }
}

void start_ota_task(void){
    ESP_LOGI(TAG, "starting ota task");
    esp_log_level_set("HTTP_CLIENT", ESP_LOG_INFO);
    // esp_log_level_set("esp_https_ota", ESP_LOG_DEBUG);
    // esp_log_level_set("esp_ota_ops", ESP_LOG_DEBUG);
    // esp_log_level_set("MQTT_CLIENT", ESP_LOG_INFO);

    event_group = xEventGroupCreate();
    xEventGroupClearBits(event_group,OTA_UNBLOCKED);
    
    ensure_dspic_updated();
    validate_booted_image();

    static uint8_t ucParameterToPass = {0};
    TaskHandle_t taskHandle = NULL;
    int stack_size = 4096*2;
    xTaskCreate( 
        ota_task, "otatask", stack_size, 
        &ucParameterToPass, 5, &taskHandle
    );
    ESP_LOGD(TAG, "...");

    int32_t dspic_len = dspic_bin_end - dspic_bin_start;
    ESP_LOGD(TAG, "dsPIC binary is %d bytes", dspic_len);
    ESP_LOGD(TAG, ">>>>dsPIC image crc32 is %d", rc_crc32(0,(const char *) dspic_bin_start, dspic_len));
}

int start_ota(void){
    xEventGroupSetBits(event_group, OTA_UNBLOCKED);
    return 0;
}