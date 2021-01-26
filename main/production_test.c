#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "freertos/event_groups.h"

//#include "https_client.h"
#include "production_test.h"
#include "DeviceInfo.h"
#include "i2cDevices.h"
#include "EEPROM.h"
#include "RTC.h"
//#include "storage.h"
#include "network.h"
#include "eeprom_wp.h"

#include "protocol_task.h"
#include "audioBuzzer.h"
#include "CLRC661.h"
#include "at_commands.h"
#include "ppp_task.h"
#include "protocol_task.h"

//#include "adc_control.h"

#include "lwip/sockets.h"

static const char *TAG = "PROD-TEST :";


//static bool connected = false;

static bool prodtest_running = false;

bool prodtest_active(){
	return prodtest_running;
}

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
		.url = "http://10.253.73.97:8585/get/mac",//Used at Internal

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

			eeprom_wp_disable_nfc_disable();
			esp_err_t err = i2cWriteDeviceInfoToEEPROM(prodDevInfo);
			eeprom_wp_enable_nfc_enable();

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

void prodtest_send(char *payload)
{
	int err = send(sock, payload, strlen(payload), 0);
	if (err < 0) {
		ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
		return;
	}
}

int await_prodtest_external_step_acceptance(){
	char rx_buffer[1];

	for (int i = 0; i < 100; i++){

		int len = recv(sock, rx_buffer, sizeof(rx_buffer), MSG_WAITALL);

		if (len < 0) {
			if(errno == 11){
				//workaround, this error should never happen on a blocking socket
				i--;
				continue;
			}
			ESP_LOGE(TAG, "recv failed: errno %d", errno);
		} else {
			if(rx_buffer[0]=='*'){
				ESP_LOGI(TAG, "got external acceptance for test step");
				return 0;
			}else{
				ESP_LOGW(TAG, "ignoring unexpected data on socket");
			}
		}

	}

	return -1;
}

static struct TagInfo latest_tag = {0};
static EventGroupHandle_t prodtest_eventgroup;
static const int NFC_READ = BIT0;

int prodtest_on_nfc_read(){
	ESP_LOGI(TAG, "nfctag submitted to prodtest procedure");
	latest_tag = NFCGetTagInfo();
	xEventGroupSetBits( prodtest_eventgroup, NFC_READ);
	return 0;
}

int run_component_tests();
int charge_cycle_test();
void socket_connect(void);

void prodtest_perform(struct DeviceInfo device_info)
{
	prodtest_eventgroup = xEventGroupCreate();
    xEventGroupClearBits(prodtest_eventgroup, NFC_READ);

	socket_connect();

	char payload[100];
	sprintf(payload, "Serial: %s\r\n", device_info.serialNumber);
	prodtest_send(payload);
	sprintf(payload, "0|3|Starting factory test\r\n");
	prodtest_send(payload);

	MCU_SendCommandId(CommandEnterProductionMode);

	charge_cycle_test();

	prodtest_running = true;
	ESP_LOGI(TAG, "waitinf on rfid");
	prodtest_send("0|0|waiting on rfid");
	xEventGroupWaitBits(prodtest_eventgroup, NFC_READ, pdFALSE, pdFALSE, portMAX_DELAY);
	
	sprintf(payload, "0|3|RFID: %s\r\n", latest_tag.idAsString);
	prodtest_send(payload);

	if(run_component_tests()<0){
		ESP_LOGE(TAG, "Component test error");
		sprintf(payload, "FAIL\r\n");
		prodtest_send( payload);

		goto cleanup;
	}

	
	sprintf(payload, "4|3|Factory test completed, all tests passed\r\n");
	prodtest_send( payload);

	eeprom_wp_disable_nfc_disable();
	if(EEPROM_WriteFactoryStage(FactoryStagComponentsTested)!=ESP_OK){
		ESP_LOGE(TAG, "Failed to mark test pass on eeprom");
	}

	eeprom_wp_enable_nfc_enable();
	sprintf(payload, "PASS\r\n");
	prodtest_send( payload);

	cleanup:
	vTaskDelay(pdMS_TO_TICKS(1000)); // workaround, close does not block properly??
	shutdown(sock, 0);
	close(sock);

}

int test_rtc(){

	struct tm time1 = RTCReadTime();
	vTaskDelay(pdMS_TO_TICKS(3000));
	struct tm time2 = RTCReadTime();

	if(time1.tm_sec == time2.tm_sec){
		prodtest_send("0|0|RTC test FAIL\r\n");
		return -1;
	}

	prodtest_send("0|0|RTC test PASS\r\n");
	return 0;
}

int test_bg(){

	char payload[128];

	configure_modem_for_prodtest();
	prodtest_send("0|0|BG95 startup PASS\r\n");

	char imei[20];
    at_command_get_imei(imei, 20);
	sprintf(payload, "0|3|BG imei: %s\r\n", imei);
	prodtest_send(payload);

	int activate_result = at_command_activate_pdp_context();
	if(activate_result<0){
		goto err;
	}

	int rssi; int ber;
	if(at_command_signal_quality(&rssi, &ber)<0){
		goto err;
	}
	sprintf(payload, "0|3|BG rssi: %d\r\n", rssi);
	prodtest_send(payload);

	int sent; int rcvd; int lost; int min; int max; int avg;
	int ping_error = at_command_ping_test(&sent, &rcvd, &lost, &min, &max, &avg);
	if(ping_error<0){
		goto err;
	}

	sprintf(payload, "0|3|BG ping avg: %d\r\n", avg);
	prodtest_send(payload);

	int deactivate_result = at_command_deactivate_pdp_context();
	if(deactivate_result<0){
		goto err;
	}

	prodtest_send("0|0|BG tests pass\r\n");
	return 0;

	err:
	return -1;
}

int test_leds(){
	// led should be on, the dsPIC is already in prodtest mode
	prodtest_send("0|0|Led test start\r\n");

	int result = await_prodtest_external_step_acceptance();
	if(result==0){
		ESP_LOGI(TAG, "led test accepted");
		prodtest_send("0|0|led test PASS\r\n");
		return 0;
	}else{
		prodtest_send("0|0|led test fail\r\n");
	}
	return -1;
}

int test_buzzer(){
	prodtest_send("0|0|buzzer test start\r\n");
	audio_play_nfc_card_accepted();

	int result = await_prodtest_external_step_acceptance();
	if(result==0){
		ESP_LOGI(TAG, "buzzer test accepted");
		prodtest_send("0|0|buzzer test PASS\r\n");
		return 0;
	}else{
		prodtest_send("0|0|buzzer test fail\r\n");
	}
	return -1;
}

int test_switch(){
	int switch_state = MCU_GetSwitchState();

    if(switch_state==0){
		//the switch must be in pos 0 when it leaves the factory
		prodtest_send("0|0|switch test PASS\r\n");
		return 0;
	}else if(switch_state==2){
		// for testing we will also allow switch to be in temp. wifi config postion
		prodtest_send("0|0|switch test in dev mode PASS\r\n");
		return 1;
	}else{
		prodtest_send("0|0|switch test fail\r\n");
	}

	return -1;
}

int run_component_tests(){
	ESP_LOGI(TAG, "testing components");

	if(test_buzzer()<0){
		goto err;
	}
	
	if(test_leds()<0){
		goto err;
	}

	if(test_rtc()<0){
		goto err;
	}
		
	if(test_switch()<0){
		goto err;
	}
		
	if(test_bg()<0){
		goto err;
	}

	return 0;

	err:
		return -1;
}

static const uint8_t eCAR_DISCONNECTED = 12;
static const uint8_t eCAR_CHARGING = 6;

int charge_cycle_test(){
	ESP_LOGI(TAG, "waiting for charging start");
	while(MCU_GetchargeMode()!=eCAR_CHARGING){
		ESP_LOGI(TAG, "waiting for charging start");
		vTaskDelay(pdMS_TO_TICKS(1500));
	}

	ESP_LOGI(TAG, "charging started, sampling data");

	while(MCU_GetchargeMode()!=eCAR_DISCONNECTED){
		ESP_LOGI(TAG, "Charging data:");
		ESP_LOGI(TAG, "\teMeter temp: %f, %f, %f", MCU_GetEmeterTemperature(0), MCU_GetEmeterTemperature(1), MCU_GetEmeterTemperature(2));
		ESP_LOGI(TAG, "\tVoltages: %f, %f, %f", MCU_GetVoltages(0), MCU_GetVoltages(1), MCU_GetVoltages(2));
		ESP_LOGI(TAG, "\tCurrents: %f, %f, %f", MCU_GetCurrents(0), MCU_GetCurrents(1), MCU_GetCurrents(2));
		ESP_LOGI(TAG, "\tOther temps: %f, %f", MCU_GetTemperaturePowerBoard(0), MCU_GetTemperaturePowerBoard(1));
		
		vTaskDelay(pdMS_TO_TICKS(3000));
	}

	return 0;
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
