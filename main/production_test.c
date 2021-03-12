#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "freertos/event_groups.h"
#include "esp_ota_ops.h"

//#include "https_client.h"
#include "production_test.h"
#include "DeviceInfo.h"
#include "i2cDevices.h"
#include "EEPROM.h"
#include "RTC.h"
//#include "storage.h"
#include "network.h"
#include "eeprom_wp.h"
#include "zaptec_cloud_observations.h"
#include "zaptec_cloud_listener.h"

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
int prodtest_nfc_init();
char *host_from_rfid();

bool prodtest_active(){
	return prodtest_running;
}

enum test_stage{
	TEST_STAGE_CONNECTING_WIFI = 1,
	TEST_STAGE_WAITING_RIFD = 2,
	TEST_STAGE_WAITING_PC = 3,
	TEST_STAGE_FETCHING_ID = 4,
	TEST_STAGE_RUNNING_TEST = 5,
	TEST_STAGE_WAITING_ANWER = 6,
	TEST_STAGE_ERROR = 7,
	TEST_STAGE_PASS = 8,

	TEST_STAGE_LED_DEMO = 20,
};

void set_prodtest_led_state(enum test_stage state){
	ESP_LOGI(TAG, "setting led state: %d", state);
	int reply_type = MCU_SendUint8Parameter(FactoryTestStage, state);
	ESP_LOGI(TAG, "set led state result %d", reply_type);
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

void await_ip(){
	while (network_WifiIsConnected() == false)
	{
		set_prodtest_led_state(TEST_STAGE_CONNECTING_WIFI);
		ESP_LOGE(TAG, "'Waiting for IP...");
		vTaskDelay(pdMS_TO_TICKS(1000));
	}
}

void await_mqtt(){
	while (isMqttConnected() == false)
	{
		set_prodtest_led_state(TEST_STAGE_CONNECTING_WIFI);
		ESP_LOGE(TAG, "'Waiting for MQTT...");
		vTaskDelay(pdMS_TO_TICKS(1000));
	}
}


int prodtest_getNewId()
{
	prodtest_nfc_init();
	//if(connected == false)
		//connected = network_init(true);

	await_ip();

	char url [100];
	sprintf(url, "http://%s:8585/get/mac", host_from_rfid());

	ESP_LOGI(TAG, "Finding id from %s", url);

	esp_http_client_config_t config = {
		.url = url,

		.method = HTTP_METHOD_GET,
		.event_handler = _http_event_handler,
		.transport_type = HTTP_TRANSPORT_OVER_TCP,
		.is_async = false,
		.timeout_ms = 10000,
	};

	char *buffer = malloc(512 + 1);

	esp_http_client_handle_t client = esp_http_client_init(&config);

	esp_http_client_set_header(client, "Content-Type", "text/plain");

	set_prodtest_led_state(TEST_STAGE_FETCHING_ID);
	esp_err_t err;
	if ((err = esp_http_client_open(client, 0)) != ESP_OK) {
		ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
		free(buffer);
		set_prodtest_led_state(TEST_STAGE_ERROR);
		return -1;
	}

	int content_length =  esp_http_client_fetch_headers(client);
	volatile int total_read_len = 0, read_len;


	read_len = esp_http_client_read(client, buffer, 100);
	if (read_len <= 0) {
		ESP_LOGE(TAG, "Error read data");
		set_prodtest_led_state(TEST_STAGE_ERROR);
		return -2;
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
			i2cReadDeviceInfoFromEEPROM(); // force the buffered device info to refresh
			ESP_LOGD(TAG, "eeprom updated");

			if (err != ESP_OK)
			{
				ESP_LOGE(TAG, "ERROR: Not able to save device info to EEPROM!!!");
				set_prodtest_led_state(TEST_STAGE_ERROR);
				vTaskDelay(3000 / portTICK_PERIOD_MS);
				return -4;
			
			}
		}
		else
		{
			ESP_LOGE(TAG, "ERROR: Incorrect onboarding format received form server");
			set_prodtest_led_state(TEST_STAGE_ERROR);
			return -3;
		}

	}else{
		set_prodtest_led_state(TEST_STAGE_ERROR);
		return -5;
	}

	ESP_LOGI(TAG, "HTTP Stream reader Status = %d, content_length = %d",
					esp_http_client_get_status_code(client),
					esp_http_client_get_content_length(client));
	esp_http_client_close(client);
	esp_http_client_cleanup(client);

	ESP_LOGI(TAG, "buffer = %s", buffer);

	free(buffer);

	return 0;

}

enum test_state{
		TEST_STATE_READY = -1,
		TEST_STATE_RUNNING = 0,
		TEST_STATE_SUCCESS = 1,
		TEST_STATE_FAILURE = 2,
		TEST_STATE_MESSAGE = 3,
		TEST_STATE_QUESTION = 4,
		TEST_STATE_ANSWER = 5,
		TEST_STATE_TIMESTAMP = 6,
		TEST_STATE_VERIFYSERIAL = 7,
		TEST_STATE_SERIALIZEDDATA = 8,
};

enum test_item{
	TEST_ITEM_COMPONENT_BUZZER, 
	TEST_ITEM_COMPONENT_RTC,
	TEST_ITEM_COMPONENT_LED,
	TEST_ITEM_COMPONENT_SWITCH,
	TEST_ITEM_COMPONENT_BG,
	TEST_ITEM_DEV_TEMP,
	TEST_ITEM_CHARGE_CYCLE,
	TEST_ITEM_CHARGE_CYCLE_EMETER_TEMPS,
	TEST_ITEM_CHARGE_CYCLE_EMETER_VOLTAGES,
	TEST_ITEM_CHARGE_CYCLE_EMETER_CURRENTS,
	TEST_ITEM_CHARGE_CYCLE_OTHER_TEMPS,
	TEST_ITEM_CHARGE_CYCLE_EMETER_VOLTAGES2,
};

static EventGroupHandle_t prodtest_eventgroup;
static const int NFC_READ = BIT0;
static const int SOCKET_DATA_READY = BIT1;
static const int SOCKET_DATA_SENDT = BIT2;

static int sock;
#define log_line_length_max 256
static char log_line_buffer[log_line_length_max];

int prodtest_sock_send(char *payload)
{
	ESP_LOGI(TAG, "sending to test PC:[%s]", payload);

	if(publish_prodtest_line(payload)<0){
		return -1;
	}

	strncpy(log_line_buffer, payload, log_line_length_max);
	xEventGroupSetBits(prodtest_eventgroup, SOCKET_DATA_READY);
	EventBits_t bits = xEventGroupWaitBits(prodtest_eventgroup, SOCKET_DATA_SENDT, 1, 1, pdMS_TO_TICKS(5000));
	if((bits&SOCKET_DATA_SENDT) ==0){
		ESP_LOGE(TAG, "Failed to communicate with prodtest PC" );
		return -2;
	}

	return 0;
}

int prodtest_send(enum test_state state, enum test_item item, char *message){
	char payload [100];
	sprintf(payload, "%d|%d|%s\r\n", item, state, message);
	if(prodtest_sock_send(payload)<0){
		ESP_LOGE(TAG, "PRODTEST COMMS ERROR...");
		vTaskDelay(pdMS_TO_TICKS(10*1000));
		esp_restart();
	}

	return 0;
}


int await_prodtest_external_step_acceptance(char * acceptance_string, bool indicate_with_led){
	char rx_buffer[100];
	char * next_char = rx_buffer;

	if(indicate_with_led)
		set_prodtest_led_state(TEST_STAGE_WAITING_ANWER);

	for (;;){

		int len = recv(sock, next_char, 1, MSG_WAITALL);

		if (len < 0) {
			if(errno == 11){
				//workaround, this error should never happen on a blocking socket
				ESP_LOGW(TAG, "recv failed: errno %d", errno);
				continue;
			}
			ESP_LOGE(TAG, "recv failed: errno %d, aborting", errno);
			return -10;
		} else {

			if(next_char[0]=='\n'){
				*next_char = 0;
				ESP_LOGI(TAG, "got question result line: [%s]", rx_buffer);
				/*int tail_start = strlen(rx_buffer) - (strlen(acceptance_string);
				char *line_tail = rx_buffer + tail_start;
				ESP_LOGW(TAG, "question response: [%s]", line_tail);
				*/
				if(strstr(rx_buffer, acceptance_string)){
					ESP_LOGI(TAG, "accepted");
					set_prodtest_led_state(TEST_STAGE_RUNNING_TEST);
					return 0;
				}
				ESP_LOGW(TAG, "question response not accepted");
				return -2;

			}else{
				next_char += len;
			}
		}
	}

	ESP_LOGW(TAG, "question response parsing bug detected");
	return -1;
}

static struct TagInfo latest_tag = {0};

int prodtest_on_nfc_read(){
	ESP_LOGI(TAG, "nfctag submitted to prodtest procedure");
	latest_tag = NFCGetTagInfo();
	xEventGroupSetBits( prodtest_eventgroup, NFC_READ);
	audio_play_nfc_card_accepted();
	return 0;
}

int prodtest_nfc_init(){
	if(!prodtest_running){
		prodtest_eventgroup = xEventGroupCreate();
		xEventGroupClearBits(prodtest_eventgroup, NFC_READ);
		prodtest_running = true;
	}

	return 0;
}

char *host_from_rfid(){
	if(!latest_tag.tagIsValid){
		audio_play_nfc_card_accepted();
		ESP_LOGI(TAG, "waiting for RFID");
		set_prodtest_led_state(TEST_STAGE_WAITING_RIFD);
		xEventGroupWaitBits(prodtest_eventgroup, NFC_READ, pdFALSE, pdFALSE, portMAX_DELAY);
	}

	ESP_LOGI(TAG, "using rfid tag: %s", latest_tag.idAsString);

	if(strcmp(latest_tag.idAsString, "nfc-BADBEEF2")==0)
		return "example.com";
	if(strcmp(latest_tag.idAsString, "nfc-D69E1A3B")==0) // c365
		return "192.168.0.103";
	if(strcmp(latest_tag.idAsString, "nfc-AAD61A3C")==0) // zaptec rectangular
		return "192.168.0.103";
	if(strcmp(latest_tag.idAsString, "nfc-AA8EA37D")==0) // marked with sitcker
		return "192.168.0.113";
	if(strcmp(latest_tag.idAsString, "nfc-92BDA93B")==0) // marked WC
		return "10.0.1.15";
	if(strcmp(latest_tag.idAsString, "nfc-AAA58DAC")==0) // id sent by Fredrik (1)
		return "10.0.1.15";
	if(strcmp(latest_tag.idAsString, "nfc-AA6449AC")==0) // id sent by Fredrik (2)
		return "10.0.1.15";
	if(strcmp(latest_tag.idAsString, "nfc-AAF895AC")==0) // id sent by Fredrik (3)
		return "10.0.1.15";
	if(strcmp(latest_tag.idAsString, "nfc-E234AC3B")==0)
		return "10.0.244.234";

	ESP_LOGE(TAG, "Bad rfid tag");
	return "BAD RFID TAG";
}

int run_component_tests();
int charge_cycle_test();
int check_dspic_warnings();
void socket_connect(void);

static void socket_task(void *pvParameters){

	TickType_t ping_rate = pdMS_TO_TICKS(1000);

	while(true){
		EventBits_t bits = xEventGroupWaitBits(prodtest_eventgroup, SOCKET_DATA_READY, 1, 1, ping_rate);

		if((bits&SOCKET_DATA_READY)!=0){
			int err = send(sock, log_line_buffer, strlen(log_line_buffer), 0);
			if (err < 0) {
				ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
			}
			xEventGroupSetBits(prodtest_eventgroup, SOCKET_DATA_SENDT);
		}else{
			// ping botch
			char *payload = "5|0|factory test running\r\n";
			int err = send(sock, payload, strlen(payload), 0);
			if (err < 0) {
				ESP_LOGE(TAG, "Error sending ping to prodtest pc(%d)", errno);
			}
		}
	}
}

int prodtest_perform(struct DeviceInfo device_info)
{
	prodtest_nfc_init();
	await_mqtt();
	socket_connect();

	TaskHandle_t socket_task_handle = NULL;
	xTaskCreate(socket_task, "prodtest_socket", 2048, NULL, 7, &socket_task_handle);


	bool success = false;

	char payload[100];
	sprintf(payload, "Serial: %s\r\n", device_info.serialNumber);
	prodtest_sock_send(payload);
	await_prodtest_external_step_acceptance("ACCEPTED", false);


	prodtest_send(TEST_STATE_RUNNING, TEST_ITEM_DEV_TEMP, "Factory test");
	
	sprintf(payload, "Version (gitref): %s", esp_ota_get_app_description()->version);
	prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_DEV_TEMP, payload);

	MCU_SendCommandId(CommandEnterProductionMode);

	if(
		(device_info.factory_stage<FactoryStagComponentsTested)
		|| (device_info.factory_stage == FactoryStageUnknown)
		){
		if(run_component_tests()<0){
			ESP_LOGE(TAG, "Component test error");
			prodtest_sock_send( "FAIL\r\n" );
			set_prodtest_led_state(TEST_STAGE_ERROR);
			ESP_LOGE(TAG, "Cleaning failed test");

			goto cleanup;
		}

		eeprom_wp_disable_nfc_disable();
		if(EEPROM_WriteFactoryStage(FactoryStagComponentsTested)!=ESP_OK){
			ESP_LOGE(TAG, "Failed to mark component test pass on eeprom");
			prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_DEV_TEMP, "EEPROM write failure");
			eeprom_wp_enable_nfc_enable();
			goto cleanup;
		}else{
			success = true;
			eeprom_wp_enable_nfc_enable();
		}
	}

	if(charge_cycle_test()<0){
		ESP_LOGE(TAG, "charge_cycle_test error");
		prodtest_sock_send( "FAIL\r\n");
		set_prodtest_led_state(TEST_STAGE_ERROR);
		goto cleanup;
	}

	prodtest_send(TEST_STATE_SUCCESS, TEST_ITEM_DEV_TEMP, "Factory test");

	eeprom_wp_disable_nfc_disable();
	if(EEPROM_WriteFactoryStage(FactoryStageFinnished)!=ESP_OK){
		ESP_LOGE(TAG, "Failed to mark charge cycle test pass on eeprom");
		prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_DEV_TEMP, "EEPROM write failure");
		eeprom_wp_enable_nfc_enable();
		goto cleanup;
	}else{
		success = true;
	}

	eeprom_wp_enable_nfc_enable();
	sprintf(payload, "PASS\r\n");
	prodtest_sock_send( payload);
	set_prodtest_led_state(TEST_STAGE_PASS);
	audio_play_nfc_card_accepted();

	cleanup:
	vTaskDelete(socket_task_handle);
	shutdown(sock, 0);
	vTaskDelay(pdMS_TO_TICKS(1000)); // workaround, close does not block properly??
	close(sock);

	if(success){
		return 0;
	}else{
		audio_play_nfc_card_denied();
		return -1;
	}

}

int test_rtc(){
	set_prodtest_led_state(TEST_STAGE_RUNNING_TEST);
	prodtest_send(TEST_STATE_RUNNING, TEST_ITEM_COMPONENT_RTC, "RTC");
	struct tm time1 = RTCReadTime();
	vTaskDelay(pdMS_TO_TICKS(3000));
	struct tm time2 = RTCReadTime();

	if(time1.tm_sec == time2.tm_sec){
		prodtest_send(TEST_STATE_FAILURE, TEST_ITEM_COMPONENT_RTC, "RTC");
		return -1;
	}

	prodtest_send(TEST_STATE_SUCCESS, TEST_ITEM_COMPONENT_RTC, "RTC");
	return 0;
}

void bg_log_cb(char *message){
	prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_BG, message);
}

int test_bg(){

	prodtest_send(TEST_STATE_RUNNING, TEST_ITEM_COMPONENT_BG, "BG95");
	set_prodtest_led_state(TEST_STAGE_RUNNING_TEST);
	char payload[128];

	if(configure_modem_for_prodtest(bg_log_cb)<0){
		prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_BG, "modem startup error");
		goto err;
	}
	prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_BG, "modem startup complete");

	char imei[20];
    if(at_command_get_imei(imei, 20)<0){
		prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_BG, "modem imei error");
		goto err;
	}
	sprintf(payload, "IMEI: %s\r\n", imei);
	prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_BG, payload);

	char ccid[30];
    if(at_command_get_ccid(ccid, 30)<0){
		prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_BG, "modem ccid error");
		goto err;
	}
	sprintf(payload, "CCID: %s\r\n", ccid);
	prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_BG, payload);

	// deactivate incase there already is a context
	int preventive_deactivate_result = at_command_deactivate_pdp_context();
	sprintf(payload, "pdp cleanup result: %d\r\n", preventive_deactivate_result);
	prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_BG, payload);

	int activate_result = at_command_activate_pdp_context();
	if(activate_result<0){
		vTaskDelay(pdMS_TO_TICKS(30*1000));
		prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_BG, "retrying pdp activate");
		if(at_command_activate_pdp_context()<0){
			prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_BG, "error on pdp activate");
			goto err;
		}

	}

	char sysmode[16]; int rssi; int rsrp; int sinr; int rsrq;
	if(at_command_signal_strength(sysmode, &rssi, &rsrp, &sinr, &rsrq)<0){
		goto err;
	}

	char signal_string[256];
	snprintf(signal_string, 256, "[AT+QCSQ] mode: %s, rssi: %d, rsrp: %d, sinr: %d, rsrq: %d\r\n", sysmode, rssi, rsrp, sinr, rsrq);
	prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_BG, signal_string);

	int sent; int rcvd; int lost; int min; int max; int avg;
	int ping_error = at_command_ping_test(&sent, &rcvd, &lost, &min, &max, &avg);
	if(ping_error<0){
		goto err;
	}

	sprintf(payload, "Ping avg: %d\r\n", avg);
	prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_BG, payload);

	int deactivate_result = at_command_deactivate_pdp_context();
	if(deactivate_result<0){
		goto err;
	}

	prodtest_send(TEST_STATE_SUCCESS, TEST_ITEM_COMPONENT_BG, "BG95");
	return 0;

	err:
	prodtest_send(TEST_STATE_FAILURE, TEST_ITEM_COMPONENT_BG, "BG95");
	return -1;
}

int test_leds(){
	set_prodtest_led_state(TEST_STAGE_LED_DEMO);//todo rgbw
	set_prodtest_led_state(TEST_STAGE_LED_DEMO);

	prodtest_send(TEST_STATE_RUNNING, TEST_ITEM_COMPONENT_LED, "LED");

	prodtest_send(TEST_STATE_QUESTION, TEST_ITEM_COMPONENT_LED, "LED R-G-B-W?|yes|no");
	int result = await_prodtest_external_step_acceptance("yes", false);
	if(result==0){
		ESP_LOGI(TAG, "led test accepted");
		prodtest_send(TEST_STATE_SUCCESS, TEST_ITEM_COMPONENT_LED, "LED");
		return 0;
	}else{
		prodtest_send(TEST_STATE_FAILURE, TEST_ITEM_COMPONENT_LED, "LED");
	}
	return -1;
}

int test_buzzer(){
	set_prodtest_led_state(TEST_STAGE_RUNNING_TEST);
	prodtest_send(TEST_STATE_RUNNING, TEST_ITEM_COMPONENT_BUZZER, "buzzer");


	audio_play_nfc_card_accepted();
	prodtest_send(TEST_STATE_QUESTION, TEST_ITEM_COMPONENT_BUZZER, "buzzed?|yes|no");

	int result = await_prodtest_external_step_acceptance("yes", true);
	if(result==0){
		ESP_LOGI(TAG, "buzzer test accepted");
		prodtest_send(TEST_STATE_SUCCESS, TEST_ITEM_COMPONENT_BUZZER, "buzzer");
		return 0;
	}else{
		prodtest_send(TEST_STATE_FAILURE, TEST_ITEM_COMPONENT_BUZZER, "buzzer");
	}
	return -1;
}

int test_switch(){
	set_prodtest_led_state(TEST_STAGE_RUNNING_TEST);
	prodtest_send(TEST_STATE_RUNNING, TEST_ITEM_COMPONENT_SWITCH, "Rotary Switch");
	int switch_state = MCU_GetSwitchState();

    if(switch_state==0){
		//the switch must be in pos 0 when it leaves the factory
		prodtest_send(TEST_STATE_SUCCESS, TEST_ITEM_COMPONENT_SWITCH, "Rotary Switch");
		return 0;
	}else if(switch_state==2){
		// for testing we will also allow switch to be in temp. wifi config postion
		prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_SWITCH, "Rotary Switch pass with dev mode exception");
		prodtest_send(TEST_STATE_SUCCESS, TEST_ITEM_COMPONENT_SWITCH, "Rotary Switch");
		return 1;
	}else{
		prodtest_send(TEST_STATE_FAILURE, TEST_ITEM_COMPONENT_SWITCH, "Rotary Switch");	
	}

	return -1;
}

int run_component_tests(){
	ESP_LOGI(TAG, "testing components");
	
	if(test_bg()<0){
		goto err;
	}

	if(test_leds()<0){
		goto err;
	}

	if(test_buzzer()<0){
		goto err;
	}

	if(test_rtc()<0){
		goto err;
	}
		
	if(test_switch()<0){
		goto err;
	}
		
	

	return 0;

	err:
		return -1;
}

static const uint8_t eCAR_DISCONNECTED = 12;
static const uint8_t eCAR_CHARGING = 6;

int charge_cycle_test(){

	prodtest_send(TEST_STATE_RUNNING, TEST_ITEM_CHARGE_CYCLE, "Charge cycle");

	if(check_dspic_warnings()<0){
		return -1;
	}

	char payload[100];

	ESP_LOGI(TAG, "charging started, sampling data"); 
	set_prodtest_led_state(TEST_STATE_RUNNING);
	float emeter_temps[] = {MCU_GetEmeterTemperature(0), MCU_GetEmeterTemperature(1), MCU_GetEmeterTemperature(2)};
	float emeter_voltages[] = { MCU_GetVoltages(0), MCU_GetVoltages(1), MCU_GetVoltages(2)};
	float emeter_currents[] = { MCU_GetCurrents(0), MCU_GetCurrents(1), MCU_GetCurrents(2)};
	float board_temps[] = {MCU_GetTemperaturePowerBoard(0), MCU_GetTemperaturePowerBoard(1)};

	prodtest_send(TEST_STATE_RUNNING, TEST_ITEM_CHARGE_CYCLE_EMETER_TEMPS, "eMeter temps");
	sprintf(payload, "Emeter temps: %f, %f, %f", emeter_temps[0], emeter_temps[1], emeter_temps[2]);
	prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_CHARGE_CYCLE_EMETER_TEMPS, payload );
	float temperature_min = 1.0; 
	float temperature_max = 99.0;
	if(emeter_temps[0] < temperature_min || emeter_temps[1]  < temperature_min || emeter_temps[2] < temperature_min
	|| emeter_temps[0] > temperature_max || emeter_temps[1] >  temperature_max || emeter_temps[2] > temperature_max){
		prodtest_send(TEST_STATE_FAILURE, TEST_ITEM_CHARGE_CYCLE_EMETER_TEMPS, "eMeter temps");
		return -1;
	}else{
		prodtest_send(TEST_STATE_SUCCESS, TEST_ITEM_CHARGE_CYCLE_EMETER_TEMPS, "eMeter temps");
	}

	prodtest_send(TEST_STATE_RUNNING, TEST_ITEM_CHARGE_CYCLE_EMETER_VOLTAGES, "eMeter voltages");
	sprintf(payload, "Emeter voltages: %f, %f, %f", emeter_voltages[0], emeter_voltages[1], emeter_voltages[2]);
	prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_CHARGE_CYCLE_EMETER_VOLTAGES, payload );
	float volt_min = -1.0; 
	float volt_max = 50.0;
	if(emeter_voltages[0] < volt_min || emeter_voltages[1]  < volt_min || emeter_voltages[2] < volt_min
	|| emeter_voltages[0] > volt_max || emeter_voltages[1] >  volt_max || emeter_voltages[2] > volt_max){
		prodtest_send(TEST_STATE_FAILURE, TEST_ITEM_CHARGE_CYCLE_EMETER_VOLTAGES, "eMeter voltages");
		return -1;
	}else{
		prodtest_send(TEST_STATE_SUCCESS, TEST_ITEM_CHARGE_CYCLE_EMETER_VOLTAGES, "eMeter voltages");
	}

	prodtest_send(TEST_STATE_RUNNING, TEST_ITEM_CHARGE_CYCLE_EMETER_CURRENTS, "eMeter currents");
	sprintf(payload, "Emeter currents: %f, %f, %f", emeter_currents[0], emeter_currents[1], emeter_currents[2]);
	prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_CHARGE_CYCLE_EMETER_CURRENTS, payload );
	float current_min = -1.0; 
	float current_max = 40.0;
	if(emeter_currents[0] < current_min || emeter_currents[1]  < current_min || emeter_currents[2] < current_min
	|| emeter_currents[0] > current_max || emeter_currents[1] >  current_max || emeter_currents[2] > current_max){
		prodtest_send(TEST_STATE_FAILURE, TEST_ITEM_CHARGE_CYCLE_EMETER_CURRENTS, "eMeter currents");
		return -1;
	}else{
		prodtest_send(TEST_STATE_SUCCESS, TEST_ITEM_CHARGE_CYCLE_EMETER_CURRENTS, "eMeter currents");
	}

	prodtest_send(TEST_STATE_RUNNING, TEST_ITEM_CHARGE_CYCLE_OTHER_TEMPS, "board temps");
	sprintf(payload, "board temps: %f, %f", board_temps[0], board_temps[1]);
	prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_CHARGE_CYCLE_OTHER_TEMPS, payload );
	if(board_temps[0] < temperature_min || board_temps[1]  < temperature_min 
	|| board_temps[0] > temperature_max || board_temps[1] >  temperature_max){
		prodtest_send(TEST_STATE_FAILURE, TEST_ITEM_CHARGE_CYCLE_OTHER_TEMPS, "board temps");
		return -1;
	}else{
		prodtest_send(TEST_STATE_SUCCESS, TEST_ITEM_CHARGE_CYCLE_OTHER_TEMPS, "board temps");
	}

	ESP_LOGI(TAG, "Pre charging data:");
	ESP_LOGI(TAG, "\teMeter temp: %f, %f, %f", MCU_GetEmeterTemperature(0), MCU_GetEmeterTemperature(1), MCU_GetEmeterTemperature(2));
	ESP_LOGI(TAG, "\tVoltages: %f, %f, %f", MCU_GetVoltages(0), MCU_GetVoltages(1), MCU_GetVoltages(2));
	ESP_LOGI(TAG, "\tCurrents: %f, %f, %f", MCU_GetCurrents(0), MCU_GetCurrents(1), MCU_GetCurrents(2));
	ESP_LOGI(TAG, "\tOther temps: %f, %f", MCU_GetTemperaturePowerBoard(0), MCU_GetTemperaturePowerBoard(1));

	if(check_dspic_warnings()<0){
		return -1;
	}

	prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_CHARGE_CYCLE, "Waiting for charging start");
	ESP_LOGI(TAG, "waiting for charging start");
	set_prodtest_led_state(TEST_STAGE_WAITING_ANWER);
	while(MCU_GetchargeMode()!=eCAR_CHARGING){
		ESP_LOGI(TAG, "waiting for charging start");

		vTaskDelay(pdMS_TO_TICKS(1500));
	}

	prodtest_send(TEST_STATE_QUESTION, TEST_ITEM_CHARGE_CYCLE, "Handle locked?|Yes|No");
	int locked_result = await_prodtest_external_step_acceptance("Yes", true);
	if(locked_result != 0){
		prodtest_send(TEST_STATE_FAILURE, TEST_ITEM_CHARGE_CYCLE, "Charge cycle");
		return -1;
	}

	if(check_dspic_warnings()<0){
		return -1;
	}

	float emeter_voltages2[] = { MCU_GetVoltages(0), MCU_GetVoltages(1), MCU_GetVoltages(2)};
	prodtest_send(TEST_STATE_RUNNING, TEST_ITEM_CHARGE_CYCLE_EMETER_VOLTAGES2, "eMeter voltages2");
	sprintf(payload, "Emeter voltages2: %f, %f, %f", emeter_voltages2[0], emeter_voltages2[1], emeter_voltages2[2]);
	prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_CHARGE_CYCLE_EMETER_VOLTAGES2, payload );
	float volt_min2 = 200.0; 
	float volt_max2 = 260.0;
	if(emeter_voltages2[0] < volt_min2 || emeter_voltages2[0] > volt_max2 ){
		prodtest_send(TEST_STATE_FAILURE, TEST_ITEM_CHARGE_CYCLE_EMETER_VOLTAGES2, "eMeter voltages2");
		return -1;
	}else{
		prodtest_send(TEST_STATE_SUCCESS, TEST_ITEM_CHARGE_CYCLE_EMETER_VOLTAGES2, "eMeter voltages2");
	}

	prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_CHARGE_CYCLE, "Waiting for handle disconnect");

	while(MCU_GetchargeMode()!=eCAR_DISCONNECTED){	
		vTaskDelay(pdMS_TO_TICKS(1000));
	}

	if(check_dspic_warnings()<0){
		return -1;
	}

	prodtest_send(TEST_STATE_SUCCESS, TEST_ITEM_CHARGE_CYCLE, "Charge cycle");

	return 0;
}


typedef struct {
    const uint8_t bit;
    const char* name;
} dspic_warning_name;

const dspic_warning_name dspic_warning_names[] = {
    {0, "HUMIDITY"},
    {1, "TEMPERATURE"},
    {2, "TEMPERATURE_ERROR"},
    {20, "FPGA_VERSION"},
    {9, "WARNING_NO_SWITCH_POW_DEF"},
    {21, "FPGA_UNEXPECTED_RELAY"},
    {22, "FPGA_CHARGING_RESET"},
    {28, "FPGA_WATCHDOG"},
    {3, "EMETER_NO_RESPONSE"},
    {25, "EMETER_LINK"},
    {24, "EMETER_ALARM"},
    {5, "CHARGE_OVERCURRENT"},
    {26, "NO_VOLTAGE_L1"},
    {27, "NO_VOLTAGE_L2_L3"},
    {7, "12V LOW LEVEL"},    
    {6, "PILOT_STATE"},
    {8, "PILOT_LOW_LEVEL"},
    {23, "PILOT_NO_PROXIMITY"},
    //{10, "REBOOT"},
    //{11, "DISABLED"},
    //{31, "VARISCITE"},
    {12, "RCD_6MA"},
    {13, "RCD_30MA"},
    {14, "RCD_PEAK"},
    {16, "RCD_TEST_AC"},
    {17, "RCD_TEST_DC"},
    {18, "RCD_FAILURE"},
    {19, "RCD_TEST_TIMEOUT"},
};

int check_dspic_warnings()
{                    

	uint32_t warnings = MCU_GetWarnings();

	if(warnings == 0){
		prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_CHARGE_CYCLE, "no errors on dspic" );
		return 0;
	}

	char payload[100];

	sprintf(payload, "warning mask: 0x%x", warnings);
	prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_CHARGE_CYCLE, payload );

	uint8_t i = 0;
	for(i = 0; i<32; i++) {
		if((warnings & (1l<<i)) != 0) {
			uint8_t t;
			for(t = 0; t<(sizeof(dspic_warning_names) / sizeof(dspic_warning_names[0])); t++) {
				if(i == dspic_warning_names[t].bit) {
	                sprintf(payload, "dsPIC warning: %s (error code: %d <> %d)", dspic_warning_names[t].name, i, t);
					prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_CHARGE_CYCLE, payload);
					break;
				}
			}
			
			if(t == (sizeof(dspic_warning_names) / sizeof(dspic_warning_names[0]))){
				sprintf(payload, "dsPIC warning: NAME NOT DEFINED (error code: %d <> %d)", i, t);
				prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_CHARGE_CYCLE, payload);
			}
			
		}
	}
	return -1;
}

void socket_connect(void){
	set_prodtest_led_state(TEST_STAGE_WAITING_PC);
	char addr_str[128];
    int addr_family;
    int ip_protocol;

    char HOST_IP_ADDR[64];
	strcpy(HOST_IP_ADDR, host_from_rfid());
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
				ESP_LOGW(TAG, "Unable to create socket: errno %d", errno);
				continue;
			}
			ESP_LOGI(TAG, "Socket created, connecting to %s:%d", HOST_IP_ADDR, PORT);

			vTaskDelay(pdMS_TO_TICKS(1000));

			volatile int err = connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
			if (err != 0) {
				ESP_LOGE(TAG, "Socket unable to connect: errno %d", errno);
				close(sock);
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
