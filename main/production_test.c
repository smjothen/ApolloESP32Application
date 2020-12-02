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
//#include "storage.h"
#include "network.h"

//#include "adc_control.h"

#include "lwip/sockets.h"

static const char *TAG = "PROD-TEST :";

static uint8_t productionSetup = 0;

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


void prodtest_getNewId()
{
	//if(connected == false)
		//connected = network_init(true);

	while (network_WifiIsConnected() == false)
	{
		ESP_LOGE(TAG, "Waiting for IP...");
		vTaskDelay(pdMS_TO_TICKS(1000));
	}

	esp_http_client_config_t config = {
		//.url = "http://10.0.1.4:8585/get/mac",//Used at WestControl
		.url = "http://10.253.73.98:8585/get/mac",//Used at Internal

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

uint8_t prodtest_getState()
{
	return productionSetup;
}

uint8_t prodtest_init()
{
	productionSetup = false;
	char checkUniqueId[10];

	//esp_err_t err = storage_clearRegistrationParameters();
	//storage_clearFactoryParameters();//TODO

//	int idLen = storage_readFactoryUniqueId(checkUniqueId);
//
//	//idLen = 0;
//	//Check UniqueId. Write new if none exist
//
//	while(idLen < 9)
//	{
//		prod_getNewId();
//		productionSetup = 1;
//		idLen = storage_readFactoryUniqueId(checkUniqueId);
//	}
//
//	ESP_LOGI(TAG, "ZAPTEC ID: %s, Setup: %d",checkUniqueId, productionSetup);
//
//	volatile uint8_t testState = 0;
//	esp_err_t err = storage_readFactoryTestState(&testState);
//
//	ESP_LOGI(TAG, "testState from flash: %d",testState);
//
////testState = 1;//TODO debug
//
//	if((testState == 1) && (err == ESP_OK))
//	{
//		productionSetup = 1;
//		while (connected == false){
//			connected = network_init(true);
//			vTaskDelay(pdMS_TO_TICKS(1000));
//		}
//	}

	return productionSetup;
}


static int sock;
static char payload[100] = {0};

void prodtest_send(bool doReceive)
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

	memset(payload, 0, 100);
}

void prodtest_perform()
{
//	if (connected == false)
//		connected = network_init(true);
//
//	if (connected == false)
//	{
//		ESP_LOGE(TAG, "No wifi");
//		return;
//	}


    char addr_str[128];
    int addr_family;
    int ip_protocol;

    char HOST_IP_ADDR[] = "10.0.1.4";//"10.0.2.13";
    int PORT = 8181;

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

    	//Read id from flash to verify that it has been saved
    	char id[10] = {0};
    	///storage_readFactoryUniqueId(id);
    	if (id[0] == 'a')
    		id[0] = 'A';
    	if (id[1] == 'p')
    		id[1] = 'P';
    	if (id[2] == 'm')
    		id[2] = 'M';
    	sprintf(payload, "Serial: %s\r\n", id);
    	prodtest_send(1);
    	sprintf(payload, "0|3|Starting factory test\r\n");
    	prodtest_send(0);

//		ESP_LOGI(TAG, "Sending 1");
//		int err = send(sock, payload, strlen(payload), 0);
//		if (err < 0) {
//			ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
//			vTaskDelay(pdMS_TO_TICKS(3000));
//			continue;
//		}
//
//		int len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
//		// Error occurred during receiving
//		if (len < 0) {
//			ESP_LOGE(TAG, "recv failed: errno %d", errno);
//			vTaskDelay(pdMS_TO_TICKS(3000));
//			continue;
//		}
//		// Data received
//		else {
//			rx_buffer[len] = 0; // Null-terminate whatever we received and treat like a string
//			ESP_LOGI(TAG, "Received %d bytes from %s:", len, addr_str);
//			ESP_LOGI(TAG, "%s", rx_buffer);
//		}

	productionSetup = 2;

	int testCounter = 0;

	//char payload[100];
//	sprintf(buf, "%s, #%d ", trigType, measurementNo);
//
//		if(controlParameters.TransmitInterval >= 60)
//			sprintf(buf+strlen(buf), "T#%dm%ds, ", controlParameters.TransmitInterval/60, controlParameters.TransmitInterval % 60);
//		else
//			sprintf(buf+strlen(buf), "T#%ds, ", controlParameters.TransmitInterval);
//
//		sprintf(buf+strlen(buf), "%.1f, AMAX %0.1fA, AVG %d, ", controlParameters.TransmitThreshold, controlParameters.MaxCurrent, controlParameters.Average);


	sprintf(payload, "0|0|EnergyLevel\r\n");
	prodtest_send(0);


	while(testCounter != 1)
	{

//		uint8_t energy = GetHANEnergyLevel();
//		if(energy > 20)
//		{
//			ESP_LOGI(TAG, "EnergyLevel: OK: %d >= 20", energy);
//			sprintf(payload, "0|1|EnergyLevel\r\n");
//			prodtest_send(0);
//			testCounter++;
//		}
//		else
//		{
//			ESP_LOGE(TAG, "EnergyLevel: FAILED: %d < 20", energy);
//			sprintf(buf, "0|2|EnergyLevel: FAILED: %d < 20", energy);
//		}
		if(testCounter != 1)
		{
			vTaskDelay(pdMS_TO_TICKS(3000));

			sprintf(payload, "PING\r\n");
			prodtest_send(1);
		}
	}

	sprintf(payload, "1|0|HwIdLevel\r\n");
	prodtest_send(0);

	while(testCounter != 2)
	{
//		float hwIdLevel = GetHwIdVoltageLevel();
//		if((3.0 > hwIdLevel) && (hwIdLevel > 2.0))
//		{
//			ESP_LOGI(TAG, "HwIdLevel: OK: 3.0 > %.2f > 2.0", hwIdLevel);
//			sprintf(payload, "1|1|HwIdLevel\r\n");
//			prodtest_send(0);
//			testCounter++;
//		}
//		else
//		{
//			ESP_LOGE(TAG, "HwIdLevel: FAILED: %.2f != [3.0, 2.0]", hwIdLevel);
//			sprintf(payload, "1|2|HwIdLevel: FAILED: %.2f != [3.0, 2.0]", hwIdLevel);
//		}

		if(testCounter != 2)
		{
			vTaskDelay(pdMS_TO_TICKS(3000));

			sprintf(payload, "PING\r\n");
			prodtest_send(1);
		}
	}

	sprintf(payload, "1|1|HwIdLevel\r\n");
	prodtest_send(0);

	productionSetup = 3;

	sprintf(payload, "2|0|Registering APM\r\n");
	prodtest_send(0);

	///APM_Init();

	sprintf(payload, "2|1|Registering APM\r\n");
	prodtest_send(0);

	sprintf(payload, "3|0|Connection check\r\n");
	prodtest_send(0);

	bool success = false;
	while (success == false)
	{
		float voltages[3] = {0.0, 0.0, 0.0};
		float currents[3] = {0.0, 0.0, 0.0};
		///success = APM_MakeMeasurementPackage(currents, voltages, "PROD", -1); //-1 flag adds MAC addresses to PROD-message

		if (success == true)
		{
			///storage_SaveFactoryTestState(2);
			ESP_LOGI(TAG, "Factory test: OK");
			sprintf(payload, "3|0|Connection check\r\n");
			prodtest_send(0);
			productionSetup = 4;
		}
//		else
//		{
//			ESP_LOGI(TAG, "Factory test: FAILED");
//			sprintf(payload, "2|2|Cloud connection FAILED");
//		}


		if(success == false)
		{
			vTaskDelay(pdMS_TO_TICKS(15000));
			ESP_LOGI(TAG, "Retrying connection check");
		}
	}

	sprintf(payload, "3|1|Connection check\r\n");
	prodtest_send(0);

	sprintf(payload, "4|3|Factory test completed, all tests passed\r\n");
	prodtest_send(0);

	sprintf(payload, "PASS\r\n");
	prodtest_send(0);

	shutdown(sock, 0);
	close(sock);

}
