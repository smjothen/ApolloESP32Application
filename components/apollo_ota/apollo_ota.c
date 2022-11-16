#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "freertos/event_groups.h"
#include "string.h"

#include "apollo_ota.h"
#include "ota_location.h"
#include "segmented_ota.h"
#include "safe_ota.h"
#include "pic_update.h"
#include "ota_log.h"
#include "DeviceInfo.h"
#include "certificate.h"
#include "protocol_task.h"
#include "i2cDevices.h"
#include "ble_interface.h"

#define TAG "OTA"

//extern const uint8_t server_cert_pem_start[] asm("_binary_ca_cert_pem_start");
//extern const uint8_t server_cert_pem_end[] asm("_binary_ca_cert_pem_end");


static EventGroupHandle_t event_group;
static const int OTA_UNBLOCKED = BIT0;
static const int SEGMENTED_OTA_UNBLOCKED = BIT1;
static bool updateOnlyIfNewVersion = false;

const uint OTA_TIMEOUT_MINUTES = 12;
const uint OTA_GLOBAL_TIMEOUT_MINUTES = 60;
const uint OTA_RETRY_PAUSE_SECONDS = 30;


void on_ota_timeout( TimerHandle_t xTimer ){
    ota_log_timeout();
    vTaskDelay(pdMS_TO_TICKS(1500)); // let's give the system some time to send the log message
    esp_restart();
}

static esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ERROR:
        ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED, setting debug header");
        esp_http_client_set_header(evt->client, "Zaptec-Debug-Info", "apollo/ota/arnt/1");
        break;
    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
        break;
    case HTTP_EVENT_ON_HEADER:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
        break;
    case HTTP_EVENT_ON_DATA:
        ESP_LOGD(TAG, "got data ready to flash %d bytes", evt->data_len);
        ota_log_download_progress_debounced(evt->data_len);
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


bool otaRunning = false;

bool otaIsRunning()
{
	return otaRunning;
}

void _do_sdk_ota(char *image_location){

	bool useCert = certificate_GetUsage();

	if(!useCert)
		ESP_LOGE(TAG, "CERTIFICATES NOT USED");

    esp_http_client_config_t config = {
        .url = image_location,
        //.cert_pem = (char *)server_cert_pem_start,
        .use_global_ca_store = useCert,
		//.transport_type = HTTP_TRANSPORT_OVER_SSL,
        .event_handler = _http_event_handler,
		.timeout_ms = 20000,
		.buffer_size = 1536,
    };

    TickType_t timeout_ticks = pdMS_TO_TICKS(OTA_TIMEOUT_MINUTES*60*1000);
    TimerHandle_t local_timeout_timer = xTimerCreate( "sdk_ota_timeout", timeout_ticks, pdFALSE, NULL, on_ota_timeout );
    xTimerReset( local_timeout_timer, portMAX_DELAY );

    ota_log_download_start(image_location);
    esp_err_t ret = esp_https_ota(&config);
    if (ret == ESP_OK) {
        ota_log_flash_success();


        // give the system some time to finnish sending the log message
        // a better solution would be to detect the message sent event, 
        // though one must ensure there is a timeout, as the system NEEDS a reboot now
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_restart();
    } else {
        ota_log_lib_error();
    }

    xTimerDelete(local_timeout_timer, portMAX_DELAY);
}


static void StopOTA(TimerHandle_t timer)
{
	// Must send command to MCU to clear purple led on charger
	MCU_SendCommandId(CommandHostFwUpdateEnd);
	ble_interface_init();
	otaRunning = false;

	xTimerStop(timer, portMAX_DELAY);

	ESP_LOGI(TAG, "Conditional stop");

	xEventGroupClearBits(event_group,OTA_UNBLOCKED);
	xEventGroupClearBits(event_group,SEGMENTED_OTA_UNBLOCKED);
}

static TimerHandle_t timeout_timer;
void ota_time_left()
{
	TickType_t timeLeft;

	timeLeft = (xTimerGetExpiryTime(timeout_timer) - xTaskGetTickCount());

	ESP_LOGE(TAG, "OTA time left: %i, %s", timeLeft, xTimerIsTimerActive(timeout_timer)==pdFALSE ? "INACTIVE" : "ACTIVE");
}


static void ota_task(void *pvParameters){

    char image_location[1024] = {0};
    char image_version[16] = {0};

    TickType_t timeout_ticks = pdMS_TO_TICKS(OTA_GLOBAL_TIMEOUT_MINUTES*60*1000);
    timeout_timer = xTimerCreate( "global_ota_timeout", timeout_ticks, pdFALSE, NULL, on_ota_timeout );
    
    bool hasNewCertificate = false;

    while (true)
    {
    	size_t free_dram = heap_caps_get_free_size(MALLOC_CAP_8BIT);
		size_t low_dram = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
		ESP_LOGE(TAG, "MEM1: DRAM: %i Lo: %i", free_dram, low_dram);

        ESP_LOGI(TAG, "waiting for ota event");
        EventBits_t ota_selection_field = xEventGroupWaitBits(
            event_group, OTA_UNBLOCKED | SEGMENTED_OTA_UNBLOCKED, 
            pdFALSE, pdFALSE, portMAX_DELAY
        );

        otaRunning = true;

        ESP_LOGW(TAG, "attempting ota update");

        //Reactivate the reset timeout if it is inactive
        if(xTimerIsTimerActive(timeout_timer)==pdFALSE){
            xTimerReset( timeout_timer, portMAX_DELAY );
        }

        ota_log_location_fetch();

        int ret = get_image_location(image_location,sizeof(image_location), image_version);
        // strcpy( image_location,"http://api.zaptec.com/api/firmware/6476103f-7ef9-4600-9450-e72a282c192b/download");
        // strcpy( image_location,"https://api.zaptec.com/api/firmware/ZAP000001/current");
        ESP_LOGI(TAG, "image location to use: %s, err: %d", image_location, ret);

        if(ret == 0x2700)
        {

			log_message("OTA certificate error 0x2700, downloading new");
			ESP_LOGW(TAG, "Updating certificate, expired at OTA");

			certificate_update(0);

			int nrOfChecks = 0;
			for (nrOfChecks = 0; nrOfChecks < 30; nrOfChecks++)
			{
				hasNewCertificate = certificate_CheckIfReceivedNew();
				if(hasNewCertificate == true)
					break;
				else
					log_message("Waiting for new certificate");

				vTaskDelay(pdMS_TO_TICKS(3000));
			}

			if(hasNewCertificate == true)
			{
				log_message("Retrying with new certificate");
				continue;
			}

			log_message("Timed out waiting for certificate. Aborting OTA");

			StopOTA(timeout_timer);

			continue;
        }

        ESP_LOGI(TAG, "Charger version: %s Cloud version: %s", GetSoftwareVersion(), image_version);
        if(updateOnlyIfNewVersion == true)
        {
        	int cmp = strcmp(GetSoftwareVersion(), image_version);
        	if(cmp == 0)
        	{
        		ESP_LOGI(TAG, "Same version -> aborting");
        		updateOnlyIfNewVersion = false;

        		StopOTA(timeout_timer);

        		continue;
        	}
        	else
        	{
        		// Must send command to MCU to set purple led on charger
        		MCU_SendCommandId(CommandHostFwUpdateStart);
        	}
        }


        // For chargers with prefix ZGB, don't allow download of older ZAP-only firmware versions!
        if(i2cSerialIsZGB() == true)
        {
        	// Deny all 0.X.X.X and 1.X.X.X versions. New versions must be at least "2.0.0.0"
        	if((strnstr(image_version, "0.", 2) != NULL) || ((strnstr(image_version, "1.", 2)) != NULL))
			{
        		log_message("Not allowed to download ZAP-only version to ZGB!");

				StopOTA(timeout_timer);
				continue;
			}
        	else
        	{
        		ESP_LOGW(TAG, "ZGB compatible version: %s", image_version);
        	}
        }


    	free_dram = heap_caps_get_free_size(MALLOC_CAP_8BIT);
		low_dram = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
		ESP_LOGE(TAG, "MEM2: DRAM: %i Lo: %i", free_dram, low_dram);

        if((ota_selection_field & OTA_UNBLOCKED) != 0 ){
            _do_sdk_ota(image_location);

            StopOTA(timeout_timer);

        }else if((ota_selection_field & SEGMENTED_OTA_UNBLOCKED) != 0){
            //do_segmented_ota(image_location);
        	do_safe_ota(image_location);

            StopOTA(timeout_timer);

        }else{
            ESP_LOGE(TAG, "Bad ota selection, what did you do??");
        }
        
    	free_dram = heap_caps_get_free_size(MALLOC_CAP_8BIT);
		low_dram = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
		ESP_LOGE(TAG, "MEM3: DRAM: %i Lo: %i", free_dram, low_dram);

        vTaskDelay(pdMS_TO_TICKS(OTA_RETRY_PAUSE_SECONDS*1000));
    }
}

static bool hasBeenUpdated = false;

void validate_booted_image(void){
    const esp_partition_t * partition = esp_ota_get_running_partition();
    ESP_LOGI(TAG, "Checking if VALID on partition %s ", partition->label);

    int dspic_update_success = update_dspic();

    if(dspic_update_success<0){
            ESP_LOGE(TAG, "FAILED to update dsPIC, restarting now...");
            // We failed to bring the dsPIC app to the version embedded in this code
            // On next reboot we will roll back, and the old dsPIC app will be flashed
            // TODO: should we restart now?
            esp_restart();
    }

    esp_ota_img_states_t ota_state;
    esp_err_t ret = esp_ota_get_state_partition(partition, &ota_state);

    if(ota_state == ESP_OTA_IMG_PENDING_VERIFY)
    {
        ESP_LOGI(TAG, "we booted a new image, lets make sure the dsPIC has the FW from this image");
        if(dspic_update_success<0){
            // could we use other error handeling here? Or should everything be handeled above?
        }else{
            ret = esp_ota_mark_app_valid_cancel_rollback();
            if(ret != ESP_OK){
                ESP_LOGE(TAG, "marking partition as valid failed with: %d", ret);
            }else{
                ESP_LOGI(TAG, "partition marked as valid");
                hasBeenUpdated = true;
            }

        }
        
    }
    else
    {
        ESP_LOGI(TAG, "partition already valid");
    }
}

bool ota_CheckIfHasBeenUpdated()
{
	return hasBeenUpdated;
}


void start_ota_task(void){
    ESP_LOGI(TAG, "starting ota task");

    esp_log_level_set("HTTP_CLIENT", ESP_LOG_INFO);
    // esp_log_level_set("esp_https_ota", ESP_LOG_DEBUG);
    // esp_log_level_set("esp_ota_ops", ESP_LOG_DEBUG);
    // esp_log_level_set("MQTT_CLIENT", ESP_LOG_INFO);

    event_group = xEventGroupCreate();
    xEventGroupClearBits(event_group,OTA_UNBLOCKED);
    xEventGroupClearBits(event_group,SEGMENTED_OTA_UNBLOCKED);
    

    static uint8_t ucParameterToPass = {0};
    TaskHandle_t taskHandle = NULL;
    int stack_size = 4096*2;
    xTaskCreate( 
        ota_task, "otatask", stack_size, 
        &ucParameterToPass, 7, &taskHandle
    );
    ESP_LOGD(TAG, "...");
}

int start_ota(void){
    xEventGroupSetBits(event_group, OTA_UNBLOCKED);
    return 0;
}

int start_segmented_ota(void){
    xEventGroupSetBits(event_group, SEGMENTED_OTA_UNBLOCKED);
    return 0;
}

int start_segmented_ota_if_new_version(void){
	updateOnlyIfNewVersion = true;
    xEventGroupSetBits(event_group, SEGMENTED_OTA_UNBLOCKED);
    return 0;
}



const char* OTAReadRunningPartition()
{
	const esp_partition_t * partition = esp_ota_get_running_partition();
	//ESP_LOGW(TAG, "Partition name: %s", partition->label);

	return partition->label;
}

void ota_rollback()
{
	ESP_LOGI(TAG, "Rollback to previous good partition");
	esp_ota_mark_app_invalid_rollback_and_reboot();
}

bool ota_rollback_to_factory()
{
	ESP_LOGI(TAG, "Rollback to factory");
	//Ref: https://esp32.com/viewtopic.php?t=4210

	//***********************************************************************************************
	//                                B A C K T O F A C T O R Y                                     *
	//***********************************************************************************************
	// Return to factory version.                                                                   *
	// This will set the otadata to boot from the factory image, ignoring previous OTA updates.     *
	//***********************************************************************************************

	esp_partition_iterator_t  pi ;                                  // Iterator for find
	const esp_partition_t*    factory ;                             // Factory partition
	esp_err_t                 err ;

	pi = esp_partition_find ( ESP_PARTITION_TYPE_APP,               // Get partition iterator for
							  ESP_PARTITION_SUBTYPE_APP_FACTORY,    // factory partition
							  "factory" ) ;
	if ( pi == NULL )                                               // Check result
	{
		ESP_LOGE ( TAG, "Failed to find factory partition" ) ;
	}
	else
	{
		factory = esp_partition_get ( pi ) ;                        // Get partition struct
		esp_partition_iterator_release ( pi ) ;                     // Release the iterator
		err = esp_ota_set_boot_partition ( factory ) ;              // Set partition for boot

		if ( err != ESP_OK )                                        // Check error
		{
			ESP_LOGE ( TAG, "Failed to set boot partition" ) ;
			return false;
		}
		else
		{
			//esp_restart() ;                                         // Restart ESP
			return true; //Restart after cloud response is sent.
		}
	}
	return false;
}
