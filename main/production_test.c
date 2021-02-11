#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_http_client.h"

//#include "https_client.h"
#include "production_test.h"
#include "DeviceInfo.h"
#include "i2cDevices.h"
#include "EEPROM.h"
#include "RTC.h"
//#include "storage.h"
#include "network.h"
#include "EEPROM.h"
#include "ppp_task.h"
#include "driver/gpio.h"

//#include "adc_control.h"

#include "lwip/sockets.h"

static const char *TAG = "PROD-TEST :";


//static bool connected = false;


esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
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
            if (!esp_http_client_is_chunked_response(evt->client)) {
                // Write out data
                // printf("%.*s", evt->data_len, (char*)evt->data);
            }

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


void prodtest_doOnboarding()
{
	//Turn on 4G to configure baud-rate and read SSID
	configure_uart(115200);
	cellularPinsOn();
	vTaskDelay(pdMS_TO_TICKS(10000));
	ppp_set_uart_baud_high();

	gpio_set_level(GPIO_OUTPUT_EEPROM_WP, 0);
	//Invalid EEPROM content
	prodtest_getNewId();

	gpio_set_level(GPIO_OUTPUT_EEPROM_WP, 1);

}


void prodtest_getNewId()
{
	//if(connected == false)
		//connected = network_init(true);

	network_connect_wifi(true);

	while (network_WifiIsConnected() == false)
	{
		ESP_LOGE(TAG, "Waiting for IP...");
		vTaskDelay(pdMS_TO_TICKS(1000));
	}

	esp_http_client_config_t config = {
		//.url = "http://10.0.1.4:8585/get/mac",//Used at WestControl
		.url = "http://10.253.73.105:8585/get/mac",//Used at Internal

		.method = HTTP_METHOD_GET,
		.event_handler = _http_event_handler,
		.transport_type = HTTP_TRANSPORT_OVER_TCP,
		.is_async = false,
		.timeout_ms = 10000,
	};

	char *buffer = malloc(512 + 1);

	esp_http_client_handle_t client = esp_http_client_init(&config);

	esp_http_client_set_header(client, "Content-Type", "text/plain");

	esp_err_t err;
	if ((err = esp_http_client_open(client, 0)) != ESP_OK) {
		ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
		free(buffer);
		return;
	}

	int content_length =  esp_http_client_fetch_headers(client);
	volatile int total_read_len = 0, read_len;


	read_len = esp_http_client_read(client, buffer, 100);
	if (read_len <= 0) {
		ESP_LOGE(TAG, "Error read data");
	}
	buffer[read_len] = 0;
	ESP_LOGD(TAG, "read_len = %d", read_len);

	char readId[10] = {0};
	char readPsk[45] = {0};
	char readPin[5] = {0};
	size_t size = 0;

	if(read_len >= 60)
	{
		//The string has fixed predefined format, only allow parsing if format is correct
		if((buffer[0] == 'Z') && (buffer[1] == 'A') && (buffer[2] == 'P') &&
			(buffer[9] == '|') && (buffer[54] == '|') && (buffer[59] == '|'))
		{
			struct DeviceInfo prodDevInfo = {0};

			prodDevInfo.EEPROMFormatVersion = GetEEPROMFormatVersion();
			memcpy(prodDevInfo.serialNumber, buffer, 9);
			memcpy(prodDevInfo.PSK, &buffer[10], 44);
			memcpy(prodDevInfo.Pin, &buffer[55], 4);

			ESP_LOGI(TAG, "v: %d, id: %s, psk: %s, pin: %s", prodDevInfo.EEPROMFormatVersion, prodDevInfo.serialNumber, prodDevInfo.PSK, prodDevInfo.Pin);

			esp_err_t err = i2cWriteDeviceInfoToEEPROM(prodDevInfo);
			if (err != ESP_OK)
			{
				while(true)
				{
					ESP_LOGE(TAG, "ERROR: Not able to save device info to EEPROM!!!");
					vTaskDelay(3000 / portTICK_PERIOD_MS);
				}
			}

//			esp_err_t serr = storage_SaveFactoryParameters(id, psk, pin, 1);
//
//			size = storage_readFactoryUniqueId(readId);
//			size = storage_readFactoryPsk(readPsk);
//			size = storage_readFactoryPin(readPin);
//			ESP_LOGI(TAG, "Read: id: %s, psk: %s, pin: %s", readId, readPsk, readPin);
		}
		else
		{
			ESP_LOGE(TAG, "ERROR: Incorrect onboarding format received form server");
		}

	}

	ESP_LOGI(TAG, "HTTP Stream reader Status = %d, content_length = %d",
					esp_http_client_get_status_code(client),
					esp_http_client_get_content_length(client));
	esp_http_client_close(client);
	esp_http_client_cleanup(client);

	ESP_LOGI(TAG, "buffer = %s", buffer);

	free(buffer);

}


static int sock;

void prodtest_send(char *payload, bool doReceive)
{

	char rx_buffer[30];//[128];
	int err = send(sock, payload, strlen(payload), 0);
	if (err < 0) {
		ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
		return;
	}

	if(doReceive == true)
	{
		volatile int len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
		// Error occurred during receiving
		if (len < 0) {
			ESP_LOGE(TAG, "recv failed: errno %d", errno);
			return;
		}
		// Data received
		else {
			rx_buffer[len] = 0; // Null-terminate whatever we received and treat like a string
			ESP_LOGI(TAG, "Received %d bytes:", len);
			ESP_LOGI(TAG, "%s", rx_buffer);
		}
	}
}

int run_component_tests();
void socket_connect(void);

void prodtest_perform(struct DeviceInfo device_info)
{
	socket_connect();

	char payload[100];
	sprintf(payload, "Serial: %s\r\n", device_info.serialNumber);
	prodtest_send(payload, 1);
	sprintf(payload, "0|3|Starting factory test\r\n");
	prodtest_send(payload, 0);

	if(run_component_tests()<0){
		ESP_LOGE(TAG, "Component test error");
		sprintf(payload, "FAIL\r\n");
		prodtest_send( payload, 0);

		goto cleanup;
	}

	
	sprintf(payload, "4|3|Factory test completed, all tests passed\r\n");
	prodtest_send( payload, 0);

	if(EEPROM_WriteFactoryStage(FactoryStagComponentsTested)!=ESP_OK){
		ESP_LOGE(TAG, "Failed to mark test pass on eeprom");
	}

	sprintf(payload, "PASS\r\n");
	prodtest_send( payload, 0);

	cleanup:
	shutdown(sock, 0);
	close(sock);

}

int test_rtc(){

	struct tm time1 = RTCReadTime();
	vTaskDelay(pdMS_TO_TICKS(3000));
	struct tm time2 = RTCReadTime();

	if(time1.tm_sec == time2.tm_sec){
		prodtest_send("0|0|RTC test FAIL\r\n", 0);
		return -1;
	}

	prodtest_send("0|0|RTC test PASS\r\n", 0);
	return 0;
}

int test_bg(){
	ESP_LOGE(TAG, "BG95 test not implemented");

	prodtest_send("0|0|BG95 test FAIL\r\n", 0);
	return -1;
}


int run_component_tests(){
	ESP_LOGI(TAG, "testing components");

	if(test_rtc()<0){
		goto err;
	}
		
	if(test_bg()<0){
		goto err;
	}


	return 0;

	err:
		return -1;
}

void socket_connect(void){
char addr_str[128];
    int addr_family;
    int ip_protocol;

    char HOST_IP_ADDR[] = "192.168.0.101";//"10.0.2.13";
    int PORT = 8181;
	// se data on PC with `socat TCP-LISTEN:8181,fork,reuseaddr STDIO`

    while (1) {

			struct sockaddr_in dest_addr;
			dest_addr.sin_addr.s_addr = inet_addr(HOST_IP_ADDR);
			dest_addr.sin_family = AF_INET;
			dest_addr.sin_port = htons(PORT);
			addr_family = AF_INET;
			ip_protocol = IPPROTO_IP;
			inet_ntoa_r(dest_addr.sin_addr, addr_str, sizeof(addr_str) - 1);


			sock =  socket(addr_family, SOCK_STREAM, ip_protocol);
			if (sock < 0) {
				ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
				continue;
			}
			ESP_LOGI(TAG, "Socket created, connecting to %s:%d", HOST_IP_ADDR, PORT);

			vTaskDelay(pdMS_TO_TICKS(1000));

			volatile int err = connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
			if (err != 0) {
				ESP_LOGE(TAG, "Socket unable to connect: errno %d", errno);
				continue;
			}
			ESP_LOGI(TAG, "Successfully connected");

			vTaskDelay(pdMS_TO_TICKS(1000));

			struct timeval tv;
			tv.tv_sec = 1;
			tv.tv_usec = 0;
			int err_ = setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*) &tv, sizeof(struct timeval));
			if (err != 0) {
					ESP_LOGE(TAG, "Socket unable to connect: errno %d", errno);
					vTaskDelay(pdMS_TO_TICKS(3000));
					continue;
			}

			break;
    	}
}
