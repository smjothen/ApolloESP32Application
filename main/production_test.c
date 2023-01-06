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
#include "SFH7776.h"
#include "at_commands.h"
#include "ppp_task.h"
#include "protocol_task.h"
#include "adc_control.h"
#include "efuse.h"

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


int prodtest_getNewId(bool validate_only)
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

	esp_http_client_fetch_headers(client);
	int read_len;

	read_len = esp_http_client_read(client, buffer, 100);
	if (read_len <= 0) {
		ESP_LOGE(TAG, "Error read data");
		set_prodtest_led_state(TEST_STAGE_ERROR);
		return -2;
	}
	buffer[read_len] = 0;
	ESP_LOGD(TAG, "read_len = %d", read_len);

	if(read_len >= 60)
	{
		if(validate_only){
			struct DeviceInfo eeprom_info = i2cGetLoadedDeviceInfo();
			int match = strncmp(buffer, eeprom_info.serialNumber, strlen("ZAP000000"));
			if(match!=0){
				set_prodtest_led_state(TEST_STAGE_ERROR);
				return -100;
			}
			return 1;
		}


		//The string has fixed predefined format, only allow parsing if format is correct
		if(((strncmp(buffer, "ZAP", 3) == 0) || (strncmp(buffer, "ZGB", 3) == 0)) &&
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
	TEST_ITEM_INFO,
	TEST_ITEM_COMPONENT_BG,
	TEST_ITEM_COMPONENT_LED,
	TEST_ITEM_COMPONENT_BUZZER,
	TEST_ITEM_COMPONENT_PROXIMITY,
	TEST_ITEM_COMPONENT_EFUSES,
	TEST_ITEM_COMPONENT_OPEN_RELAY,
	TEST_ITEM_COMPONENT_RTC,
	TEST_ITEM_COMPONENT_SWITCH,
	TEST_ITEM_COMPONENT_SERVO,
	TEST_ITEM_COMPONENT_SPEED_HWID,
	TEST_ITEM_COMPONENT_POWER_HWID,
	TEST_ITEM_COMPONENT_HW_TRIG,
	TEST_ITEM_COMPONENT_GRID,
	TEST_ITEM_COMPONENT_OPEN,
	TEST_ITEM_CHARGE_CYCLE_START,
	TEST_ITEM_CHARGE_CYCLE_EMETER_TEMPS,
	TEST_ITEM_CHARGE_CYCLE_EMETER_VOLTAGES,
	TEST_ITEM_CHARGE_CYCLE_EMETER_CURRENTS,
	TEST_ITEM_CHARGE_CYCLE_OTHER_TEMPS,
	TEST_ITEM_CHARGE_CYCLE_EMETER_VOLTAGES2,
	TEST_ITEM_CHARGE_CYCLE_EMETER_CURRENTS2,
	TEST_ITEM_CHARGE_CYCLE_STOP,
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
	char payload [100] = {0};
	sprintf(payload, "%d|%d|%s\r\n", item, state, message);

	/// Debug for testing subset of factory tests without socket connection
	ESP_LOGW(TAG, "%s", payload);
	//return 0;

	if(prodtest_sock_send(payload)<0){
		ESP_LOGE(TAG, "PRODTEST COMMS ERROR...RETRYING...");
		if(prodtest_sock_send(payload)<0){
			ESP_LOGE(TAG, "PRODTEST COMMS ERROR...");
			vTaskDelay(pdMS_TO_TICKS(10*1000));
			esp_restart();
		}
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
				ESP_LOGW(TAG, "Waiting for answer: errno %d", errno);
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

#ifdef CONFIG_ZAPTEC_RUN_FACTORY_TESTS
	//if(strcmp(latest_tag.idAsString, "nfc-5237AB3B")==0) // c365
	if(strcmp(latest_tag.idAsString, "nfc-530796E7")==0) // c365
		return "10.4.210.129";
#ifdef CONFIG_ZAPTEC_RUN_FACTORY_ADDITIONAL_RFID
	if(strcmp(latest_tag.idAsString, CONFIG_ZAPTEC_RUN_FACTORY_ADDITIONAL_RFID_ID)==0)
		return CONFIG_ZAPTEC_RUN_FACTORY_ADDITIONAL_RFID_IP;

#endif /* CONFIG_ZAPTEC_RUN_FACTORY_ADDITIONAL_RFID */
#endif /* CONFIG_ZAPTEC_RUN_FACTORY_TESTS */

	if(strcmp(latest_tag.idAsString, "nfc-BADBEEF2")==0)
		return "example.com";
	if(strcmp(latest_tag.idAsString, "nfc-D69E1A3B")==0) // c365
		return "192.168.0.103";
	if(strcmp(latest_tag.idAsString, "nfc-AAD61A3C")==0) // zaptec rectangular
		return "192.168.0.103";
	if(strcmp(latest_tag.idAsString, "nfc-AA8EA37D")==0) // marked with sitcker
		return "192.168.0.113";
	if(strcmp(latest_tag.idAsString, "nfc-AAAB19AC")==0) // marked with kapton
		return "192.168.0.113";
	if(strcmp(latest_tag.idAsString, "nfc-92BDA93B")==0) // marked WC
		return "10.0.1.15";
	if(strcmp(latest_tag.idAsString, "nfc-E234AC3B")==0)
		return "10.0.244.234";
	if(strcmp(latest_tag.idAsString, "nfc-AAF807AC")==0) // lab 1
		return "192.168.0.104";
	if(strcmp(latest_tag.idAsString, "nfc-AAF291AC")==0) // lab 2
		return "192.168.0.104";
	if(strcmp(latest_tag.idAsString, "nfc-AA47047D")==0) // fredrik
		return "192.168.0.104";

	//Wet line 1
	if(strcmp(latest_tag.idAsString, "nfc-AAF895AC")==0)
		return "10.0.1.15";
	if(strcmp(latest_tag.idAsString, "nfc-AA6449AC")==0)
		return "10.0.1.15";
	if(strcmp(latest_tag.idAsString, "nfc-AA9DFBDC")==0)
		return "10.0.1.15";

	//Wet line 2
	if(strcmp(latest_tag.idAsString, "nfc-AAA58DAC")==0)
		return "10.0.1.16";
	if(strcmp(latest_tag.idAsString, "nfc-AA3F18EC")==0)
		return "10.0.1.16";
	if(strcmp(latest_tag.idAsString, "nfc-AAAC96DC")==0)
		return "10.0.1.16";

	//Wet line 3
	if(strcmp(latest_tag.idAsString, "nfc-AA0615EC")==0)
		return "10.0.1.17";
	if(strcmp(latest_tag.idAsString, "nfc-AA229EDC")==0)
		return "10.0.1.17";
	if(strcmp(latest_tag.idAsString, "nfc-AA5180DC")==0)
		return "10.0.1.17";

	//Wet line 4
	if(strcmp(latest_tag.idAsString, "nfc-AA2EC4EC")==0)
		return "10.0.1.18";
	if(strcmp(latest_tag.idAsString, "nfc-AA4145EC")==0)
		return "10.0.1.18";
	if(strcmp(latest_tag.idAsString, "nfc-AA87C2DC")==0)
		return "10.0.1.18";

	//Wet line 5 (UK)
	if(strcmp(latest_tag.idAsString, "nfc-AA47BCEC")==0)
		return "10.0.1.19";
	if(strcmp(latest_tag.idAsString, "nfc-AA0598DC")==0)
		return "10.0.1.19";
	if(strcmp(latest_tag.idAsString, "nfc-AA375FEC")==0)
		return "10.0.1.19";


	ESP_LOGE(TAG, "Bad rfid tag");
	return "BAD RFID TAG";
}


int charge_cycle_test();
int check_dspic_warnings(enum test_item testItem);
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
			char payload[50];
			sprintf(payload, "%d|0|factory test running\r\n", TEST_ITEM_INFO);
			// ping botch
			//char *payload = "5|0|factory test running\r\n";
			int err = send(sock, payload, strlen(payload), 0);
			if (err < 0) {
				ESP_LOGE(TAG, "Error sending ping to prodtest pc(%d)", errno);
			}
		}
	}
}

static bool onePhaseTest = false;
int prodtest_perform(struct DeviceInfo device_info, bool new_id)
{
	prodtest_nfc_init();
	await_mqtt();
	socket_connect();

	TaskHandle_t socket_task_handle = NULL;
	xTaskCreate(socket_task, "prodtest_socket", 2048, NULL, 7, &socket_task_handle);


	bool success = false;

	char payload[130];
	sprintf(payload, "Serial: %s\r\n", device_info.serialNumber);
	prodtest_sock_send(payload);
	await_prodtest_external_step_acceptance("ACCEPTED", false);

	//For testing single function
	/*test_servo();
	success = true;
	goto cleanup;*/


#ifdef CONFIG_ZAPTEC_RUN_FACTORY_TESTS
	onePhaseTest = true;
#endif

	prodtest_send(TEST_STATE_RUNNING, TEST_ITEM_INFO, "Factory test info");
	
	sprintf(payload, "Version (gitref): %s", esp_ota_get_app_description()->version);
	prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_INFO, payload);

	sprintf(payload, "Location tag %s, location host %s", latest_tag.idAsString, host_from_rfid());
	prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_INFO, payload);


	if(IsProgrammableFPGAUsed() == true)
	{
		memset(payload, 0, 130);
		MCU_GetFPGAInfo(payload, 130);
		ESP_LOGI(TAG, "%s", payload);
		prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_INFO, payload);
	}


	if(onePhaseTest)
		prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_INFO, "Running 1-phase test!!!");

	if(check_dspic_warnings(TEST_ITEM_INFO)<0)
	{
		prodtest_send(TEST_STATE_FAILURE, TEST_ITEM_INFO, "Factory test info");
		goto cleanup;
	}


	MCU_SendCommandId(CommandEnterProductionMode);

	if(!new_id){
		int id_result = prodtest_getNewId(true);
		if(id_result != 1){
			sprintf(payload, "Scanned id does not match (%d)", id_result);
	        prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_INFO, payload);
			set_prodtest_led_state(TEST_STAGE_ERROR);
			vTaskDelay(pdMS_TO_TICKS(1000)); // workaround??
			goto cleanup;
		}
	}

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
			prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_INFO, "EEPROM write failure");
			eeprom_wp_enable_nfc_enable();
			goto cleanup;
		}else{
			//success = true;
			eeprom_wp_enable_nfc_enable();
		}
	}

	if(charge_cycle_test()<0){
		ESP_LOGE(TAG, "charge_cycle_test error");
		prodtest_sock_send( "FAIL\r\n");
		set_prodtest_led_state(TEST_STAGE_ERROR);
		goto cleanup;
	}

	eeprom_wp_disable_nfc_disable();
	if(EEPROM_WriteFactoryStage(FactoryStageFinnished)!=ESP_OK){
		ESP_LOGE(TAG, "Failed to mark charge cycle test pass on eeprom");
		prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_INFO, "EEPROM write failure");
		eeprom_wp_enable_nfc_enable();
		goto cleanup;
	}else{
		success = true;
	}

	eeprom_wp_enable_nfc_enable();

	prodtest_send(TEST_STATE_SUCCESS, TEST_ITEM_INFO, "Factory test info");

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

int bg_debug_log(){
	// data to communicate with Michal @ Quectel

	char payload[128];

	char cereg[30];
    if(at_command_get_cereg(cereg, 30)<0){
		prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_BG, "modem cereg error");
		return -1;
	}
	sprintf(payload, "CEREG: %s\r\n", cereg);
	prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_BG, payload);

	char qnwinfo[30];
    if(at_command_get_qnwinfo(qnwinfo, 30)<0){
		prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_BG, "modem QNWINFO error");
		return -2;
	}
	sprintf(payload, "QNWINFO: %s\r\n", qnwinfo);
	prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_BG, payload);

	char cops[30];
    if(at_command_get_operator(cops, 30)<0){
		prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_BG, "modem cops error");
		return -3;
	}
	sprintf(payload, "COPS: %s\r\n", cops);
	prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_BG, payload);

	return 0;
}

int test_bg(){

	prodtest_send(TEST_STATE_RUNNING, TEST_ITEM_COMPONENT_BG, "BG95");
	set_prodtest_led_state(TEST_STAGE_RUNNING_TEST);
	char payload[128];

	prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_BG, "Modem starting up");

	if(configure_modem_for_prodtest(bg_log_cb)<0){
		prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_BG, "Modem startup error");
		goto err;
	}
	prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_BG, "Modem startup complete");

	char version[40];
	if(at_command_get_detailed_version(version, 40)){
		prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_BG, "Modem version read error");
		goto err;
	}

	if(
		strstr(version, "BG95M6LAR02A02_01.004.01.004")||
		strstr(version, "BG95M6LAR02A02_01.002.01.002")||
		strstr(version, "BG95M6LAR02A02_01.001.01.001")
	){
		sprintf(payload, "BG95 FW version accepted: %s\r\n", version);
		prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_BG, payload);
	}else{
		sprintf(payload, "BG95 FW version rejected: %s\r\n", version);
		prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_BG, payload);
		goto err;
	}

	char imei[20];
    if(at_command_get_imei(imei, 20)<0){
		prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_BG, "Modem imei error");
		goto err;
	}
	sprintf(payload, "IMEI: %s\r\n", imei);
	prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_BG, payload);

	char imsi[20];
	if(at_command_get_imsi(imsi, 20)<0){
		prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_BG, "Modem imsi error");
		goto err;
	}
	sprintf(payload, "IMSI: %s\r\n", imsi);
	prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_BG, payload);

	char ccid[30];
    if(at_command_get_ccid(ccid, 30)<0){
		prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_BG, "Modem ccid error");
		goto err;
	}
	sprintf(payload, "CCID: %s\r\n", ccid);
	prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_BG, payload);

	// Set and verify LTE-M only mode
	at_command_set_LTE_M_only_immediate();
	char LTEStateReply[30];
	if(at_command_get_LTE_M_only(LTEStateReply, 30) < 0)
	{
		prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_BG, "LTE mode error");
		goto err;
	}

	sprintf(payload, "LTE_STATE: %s\r\n", LTEStateReply);
	prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_BG, payload);

	// Set and verify LTE-M band limitation
	at_command_set_LTE_band_immediate();
	char LTEBandReply[100];
	if(at_command_get_LTE_band(LTEBandReply, 100) < 0)
	{
		prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_BG, "LTE band error");
		goto err;
	}

	sprintf(payload, "LTE_BAND: %s\r\n", LTEBandReply);
	prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_BG, payload);

	// deactivate incase there already is a context
	int preventive_deactivate_result = at_command_deactivate_pdp_context();
	sprintf(payload, "PDP cleanup result: %d\r\n", preventive_deactivate_result);
	prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_BG, payload);

	prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_BG, "Waiting for BG95 to REGISTER");
	for(int i = 0; i <= 40; i++){
		int registered = at_command_registered();
		if((registered == 1) || (registered == 5)){
			prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_BG, "BG REGISTERED");
			break;
		}
		else if ((registered == 0) || (registered == 2)){
			ESP_LOGW(TAG, "BG not REGISTER yet");
			sprintf(payload, "BG waited %i seconds for network registration. Status: %i\r\n", i*5, registered);
			prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_BG, payload);
		}
		//Wait to see if state change or timeout
		/*else{
			prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_BG, "Waiting for BG95 REGISTER check error");
			goto err;
		}*/

		if(i >= 40){
			prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_BG, "Timing out on BG95 network registration");
			bg_debug_log();
			goto err;
		}

		vTaskDelay(pdMS_TO_TICKS(5000));
	}

	bg_debug_log();

	int activate_result = at_command_activate_pdp_context();
	if(activate_result<0){
		at_command_status_pdp_context();
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

	int http_result = at_command_http_test();
	if(http_result<0){
		sprintf(payload, "bad http get: %d\r\n", http_result);
		prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_BG, payload);
		goto err;
	}

	prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_BG, "http get success");

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
	//set_prodtest_led_state(TEST_STAGE_LED_DEMO);

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
	prodtest_send(TEST_STATE_RUNNING, TEST_ITEM_COMPONENT_BUZZER, "Buzzer");


	audio_play_nfc_card_accepted();
	prodtest_send(TEST_STATE_QUESTION, TEST_ITEM_COMPONENT_BUZZER, "Buzzed?|yes|no");

	int result = await_prodtest_external_step_acceptance("yes", true);
	if(result==0){
		ESP_LOGI(TAG, "buzzer test accepted");
		prodtest_send(TEST_STATE_SUCCESS, TEST_ITEM_COMPONENT_BUZZER, "Buzzer");
		return 0;
	}else{
		prodtest_send(TEST_STATE_FAILURE, TEST_ITEM_COMPONENT_BUZZER, "Buzzer");
	}
	return -1;
}

#define COVER_OFF_MIN 0x0010
#define COVER_OFF_MAX 0x00a0

int test_proximity(){
	char payload[128];

	set_prodtest_led_state(TEST_STAGE_RUNNING_TEST);
	prodtest_send(TEST_STATE_RUNNING, TEST_ITEM_COMPONENT_PROXIMITY, "Proximity");

	esp_err_t err = SFH7776_detect();

	bool should_exist = (MCU_GetHwIdMCUSpeed() == 3);
	if((err == ESP_OK && !should_exist) || (err == ESP_FAIL && should_exist)){
		sprintf(payload, "Proximity sensor %s", (err == ESP_OK) ? "present" : "missing");
		prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_PROXIMITY, payload);

		ESP_LOGE(TAG, "%s", payload);
		goto fail;

	}else if(!should_exist){
		sprintf(payload, "Proximity sensor not applicable");
		prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_PROXIMITY, payload);
		prodtest_send(TEST_STATE_SUCCESS, TEST_ITEM_COMPONENT_PROXIMITY, "Proximity");
		return 0;
	}

	if(SFH7776_set_mode_control(0b0100) != ESP_OK
		|| SFH7776_set_sensor_control(0b0100) != ESP_OK){

		sprintf(payload, "Unable to write sensor registers");
		prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_PROXIMITY, payload);

		ESP_LOGE(TAG, "%s", payload);
		goto fail;
	}

	vTaskDelay(pdMS_TO_TICKS(500));

	uint16_t proximity;
	if(SFH7776_get_proximity(&proximity) != ESP_OK){

		sprintf(payload, "Unable to read proximity value");
		prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_PROXIMITY, payload);

		ESP_LOGE(TAG, "%s", payload);
		goto fail;
	}

	sprintf(payload, "Proximity value %#06x.", proximity);
	prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_PROXIMITY, payload);

	//Expect cover to be off, with no clear obstruction.
	if(proximity < COVER_OFF_MIN || proximity > COVER_OFF_MAX){ // TODO: should be calibrated
		ESP_LOGE(TAG, "Proximity; %#06x", proximity);

		sprintf(payload, "Value out of range, expected %#06x - %#06x", COVER_OFF_MIN, COVER_OFF_MAX);
		prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_PROXIMITY, payload);

		goto fail;
	}


	prodtest_send(TEST_STATE_SUCCESS, TEST_ITEM_COMPONENT_PROXIMITY, "Proximity");
	return 0;

fail:
	prodtest_send(TEST_STATE_FAILURE, TEST_ITEM_COMPONENT_PROXIMITY, "Proximity");
	return -1;
}

#ifdef CONFIG_ZAPTEC_BUILD_TYPE_PRODUCTION
int test_efuses(){
	char payload[128];

	set_prodtest_led_state(TEST_STAGE_RUNNING_TEST);
	prodtest_send(TEST_STATE_RUNNING, TEST_ITEM_COMPONENT_EFUSES, "efuses");

	struct EfuseInfo efuses = {0};

	if(GetEfuseInfo(&efuses) != ESP_OK){
		sprintf(payload, "Unable to read efuses");
		prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_EFUSES, payload);

		goto fail;

	}

	sprintf(payload, "Encryption counter: %#04x, Encryption configuration: %#04x.",
		efuses.flash_crypt_cnt, efuses.encrypt_config);

	prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_EFUSES, payload);

	uint set_count = __builtin_parity(efuses.flash_crypt_cnt);
	ESP_LOGI(TAG, "Encryption cnt: %#04x, Parity: %d", efuses.flash_crypt_cnt, set_count);

	if(efuses.encrypt_config != 0xf || set_count % 2 != 1)
		goto fail;


	sprintf(payload, "Secure boot %s", efuses.enabled_secure_boot_v2 ? "enabled" : "disabled");
	prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_EFUSES, payload);

	if(!efuses.enabled_secure_boot_v2)
		goto fail;

	sprintf(payload, "UART download %s, ROM BASIC fallback %s, JTAG %s, DL encrypt %s, DL decrypt %s, DL cache %s",
		efuses.disabled_uart_download ? "Off" : "on", efuses.disabled_console_debug ? "Off" : "on",
		efuses.disabled_jtag ? "Off" : "on", efuses.disabled_dl_encrypt ? "Off" : "on",
		efuses.disabled_dl_decrypt ? "Off" : "on", efuses.disabled_dl_cache ? "Off" : "on");

	prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_EFUSES, payload);

	if(!(!efuses.disabled_uart_download && efuses.disabled_console_debug && efuses.disabled_jtag && !efuses.disabled_dl_encrypt
			&& efuses.disabled_dl_decrypt && efuses.disabled_dl_cache))
		goto fail;

	if(lock_encryption_on_if_enabled() != ESP_OK){
		sprintf(payload, "Unable to lock encryption cnt");
		prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_EFUSES, payload);

		goto fail;
	}

	prodtest_send(TEST_STATE_SUCCESS, TEST_ITEM_COMPONENT_EFUSES, "efuses");
	return 0;

fail:
	prodtest_send(TEST_STATE_FAILURE, TEST_ITEM_COMPONENT_EFUSES, "efuses");
	return -1;
}
#endif /* CONFIG_ZAPTEC_BUILD_TYPE_PRODUCTION */

int test_OPEN_relay(){
	set_prodtest_led_state(TEST_STAGE_RUNNING_TEST);

	prodtest_send(TEST_STATE_RUNNING, TEST_ITEM_COMPONENT_OPEN_RELAY, "O-PEN relay");

	prodtest_send(TEST_STATE_QUESTION, TEST_ITEM_COMPONENT_OPEN_RELAY, "Handle connected with switches OFF?|yes|no");
	int result0 = await_prodtest_external_step_acceptance("yes", false);

	MCU_SendCommandId(CommandOpenPENRelay);
	prodtest_send(TEST_STATE_QUESTION, TEST_ITEM_COMPONENT_OPEN_RELAY, "O-PEN relay open. Does multimeter show more than 10000 ohm?|yes|no");
	int result1 = await_prodtest_external_step_acceptance("yes", false);

	MCU_SendCommandId(CommandClosePENRelay);
	prodtest_send(TEST_STATE_QUESTION, TEST_ITEM_COMPONENT_OPEN_RELAY, "O-PEN relay closed. Does multimeter show less than 10 ohm?|yes|no");
	int result2 = await_prodtest_external_step_acceptance("yes", false);

	if((result0==0) && (result1==0) && (result2==0)){
		ESP_LOGI(TAG, "OPEN relay test accepted");
		prodtest_send(TEST_STATE_SUCCESS, TEST_ITEM_COMPONENT_OPEN_RELAY, "O-PEN relay");
		return 0;
	}else{
		prodtest_send(TEST_STATE_FAILURE, TEST_ITEM_COMPONENT_OPEN_RELAY, "O-PEN relay");
	}
	return -1;
}


int test_switch(){
	char payload[128];
	set_prodtest_led_state(TEST_STAGE_RUNNING_TEST);
	prodtest_send(TEST_STATE_RUNNING, TEST_ITEM_COMPONENT_SWITCH, "Rotary Switch");
	int switch_state = MCU_GetSwitchState();

	vTaskDelay(pdMS_TO_TICKS(1000));

    if(switch_state==0){
		//the switch must be in pos 0 when it leaves the factory
		prodtest_send(TEST_STATE_SUCCESS, TEST_ITEM_COMPONENT_SWITCH, "Rotary Switch");
		return 0;
	}else{
		sprintf(payload, "Switch position =  %d, should be 0", switch_state);
		prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_SWITCH, payload);
		prodtest_send(TEST_STATE_FAILURE, TEST_ITEM_COMPONENT_SWITCH, "Rotary Switch");	
	}

	return -1;
}


int test_servo(){
	char payload[128];
	set_prodtest_led_state(TEST_STAGE_RUNNING_TEST);
	prodtest_send(TEST_STATE_RUNNING, TEST_ITEM_COMPONENT_SERVO, "Servo");

	/// Calibrate to Zero position
	prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_SERVO, "Calibrating servo");
	if(MsgCommandAck == MCU_SendCommandId(CommandServoClearCalibration))
	{
		ESP_LOGW(TAG, "Sent CommandStartServoCheck OK");
		vTaskDelay(pdMS_TO_TICKS(10000));
	}
	else
	{
		ESP_LOGE(TAG, "Sent CommandStartServoCheck FAILED");
	}


	/// Check range of movement
	if(MsgCommandAck == MCU_SendCommandId(CommandStartServoCheck))
		ESP_LOGW(TAG, "Sent CommandStartServoCheck OK");
	else
		ESP_LOGE(TAG, "Sent CommandStartServoCheck FAILED");

	///Wait while the servo test is performed
	vTaskDelay(pdMS_TO_TICKS(4000));

	int16_t servoCheckStartPosition = MCU_GetServoCheckParameter(ServoCheckStartPosition);
	int16_t servoCheckStartCurrent = MCU_GetServoCheckParameter(ServoCheckStartCurrent);
	int16_t servoCheckStopPosition = MCU_GetServoCheckParameter(ServoCheckStopPosition);
	int16_t servoCheckStopCurrent = MCU_GetServoCheckParameter(ServoCheckStopCurrent);
	int servoRange = (servoCheckStartPosition-servoCheckStopPosition);
	sprintf(payload, "ServoCheck: %i, %i, %i, %i Range: %i", servoCheckStartPosition, servoCheckStartCurrent, servoCheckStopPosition, servoCheckStopCurrent, servoRange);
	ESP_LOGI(TAG, "ServoCheckParams: %s", payload);
	prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_SERVO, payload);



	//int result = await_prodtest_external_step_acceptance("yes", true);
	if(servoRange >= 110){
		ESP_LOGI(TAG, "Servo test completed");
		prodtest_send(TEST_STATE_SUCCESS, TEST_ITEM_COMPONENT_SERVO, "Servo range OK and calibrated");
		return 0;
	}else{
		sprintf(payload, "Servo: NOT ENOUGH MOVEMENT");
		prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_SERVO, payload);
		prodtest_send(TEST_STATE_FAILURE, TEST_ITEM_COMPONENT_SERVO, "Servo FAILED");
	}

	return -1;
}

/// Check for valid HW id measurement on Speed board
int test_speed_hwid(){
	set_prodtest_led_state(TEST_STAGE_RUNNING_TEST);
	prodtest_send(TEST_STATE_RUNNING, TEST_ITEM_COMPONENT_SPEED_HWID, "Speed HW ID");
	int speed_hw_id = MCU_GetHwIdMCUSpeed();

	char id_string[100];
	snprintf(id_string, 100, "Speed HW ID: %i\r\n", speed_hw_id);


/*#ifdef CONFIG_ZAPTEC_RUN_FACTORY_TESTS
	speed_hw_id = 3;
#endif*/

	prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_SPEED_HWID, id_string);

    if((speed_hw_id == 1) || (speed_hw_id == 2) || (speed_hw_id == 3) || (speed_hw_id == 4)){
		prodtest_send(TEST_STATE_SUCCESS, TEST_ITEM_COMPONENT_SPEED_HWID, id_string);
		return 0;
	}else{
		prodtest_send(TEST_STATE_FAILURE, TEST_ITEM_COMPONENT_SPEED_HWID, id_string);
	}

	return -1;
}

/// Check for valid HW id measurement on Power board
int test_power_hwid(){
	set_prodtest_led_state(TEST_STAGE_RUNNING_TEST);
	prodtest_send(TEST_STATE_RUNNING, TEST_ITEM_COMPONENT_POWER_HWID, "Power HW ID");
	int power_hw_id = MCU_GetHwIdMCUPower();

	char id_string[100];
	snprintf(id_string, 100, "Power HW ID: %i\r\n", power_hw_id);

	prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_POWER_HWID, id_string);

    if((power_hw_id == 1) || (power_hw_id == 2) || (power_hw_id == 3) || (power_hw_id == 4) || (power_hw_id == 5)){
		prodtest_send(TEST_STATE_SUCCESS, TEST_ITEM_COMPONENT_POWER_HWID, id_string);
		return 0;
	}else{
		prodtest_send(TEST_STATE_FAILURE, TEST_ITEM_COMPONENT_POWER_HWID, id_string);
	}

	return -1;
}

int test_hw_trig(){
	set_prodtest_led_state(TEST_STAGE_RUNNING_TEST);
	prodtest_send(TEST_STATE_RUNNING, TEST_ITEM_COMPONENT_HW_TRIG, "HW Trig");
	MCU_SendCommandId(CommandTestHWTrig);

	int trigResult = 0;
	int timeout = 0;
	while(timeout < 15)
	{
		vTaskDelay(pdMS_TO_TICKS(1000));

		ZapMessage rxMsgm = MCU_ReadParameter(FactoryHWTrigResult);
		if((rxMsgm.length == 1) && (rxMsgm.identifier == FactoryHWTrigResult))
		{
			trigResult = rxMsgm.data[0];
			if((trigResult == 7) || (trigResult > 0xF))
			{
				break;
			}
		}

		timeout++;
	}

	char trig_string[100];
	snprintf(trig_string, 100, "HW Trig: 0x%x (%i)\r\n", trigResult, timeout);
	prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_HW_TRIG, trig_string);

    if(trigResult == 7){
		prodtest_send(TEST_STATE_SUCCESS, TEST_ITEM_COMPONENT_HW_TRIG, trig_string);
		return 0;
	}else{
		prodtest_send(TEST_STATE_FAILURE, TEST_ITEM_COMPONENT_HW_TRIG, trig_string);
	}

	return -1;
}



int test_grid_open(){
	set_prodtest_led_state(TEST_STAGE_RUNNING_TEST);

	char result_string[100];

	if(IsUKOPENPowerBoardRevision())
	{
		/// Test O-PEN voltage measurement on O-PEN power revision
		prodtest_send(TEST_STATE_RUNNING, TEST_ITEM_COMPONENT_OPEN, "O-PEN voltage");

		float OPENVoltage = MCU_GetOPENVoltage();

		snprintf(result_string, 100, "O-PEN Voltage: %f", OPENVoltage);
		prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_OPEN, result_string );


		if(OPENVoltage < 207.0 || OPENVoltage > 253.0){
			//prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_OPEN, "O-PEN voltage");
			prodtest_send(TEST_STATE_FAILURE, TEST_ITEM_COMPONENT_OPEN, "O-PEN voltage");
			return -1;
		}

		else{
			//prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_OPEN, "O-PEN voltage");
			prodtest_send(TEST_STATE_SUCCESS, TEST_ITEM_COMPONENT_OPEN, "O-PEN voltage");
			return 0;
		}
	}
	else
	{
		/// Test Grid measurement on standard EU revisions
		prodtest_send(TEST_STATE_RUNNING, TEST_ITEM_COMPONENT_GRID, "Grid detect");

		ZapMessage rxMsg = MCU_ReadParameter(GridTestResult);
		if(rxMsg.length > 0){
			char * gtr = (char *)calloc(rxMsg.length+1, 1);
			memcpy(gtr, rxMsg.data, rxMsg.length);

			int grid_type;
			float volt_g; float volt_l12;
			int sscanf_result = sscanf(gtr, "%d: VG:%f L12:%f", &grid_type, &volt_g, &volt_l12);
			if(sscanf_result!=3){
				return -2;
			}

			sprintf(result_string, "Grid detect: %s (%d, %f, %f)", gtr, grid_type, volt_g, volt_l12);
			prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_GRID, result_string );
			free(gtr);

			if(onePhaseTest)
			{
				prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_GRID, "1-phase test (Override mode)");
				prodtest_send(TEST_STATE_SUCCESS, TEST_ITEM_COMPONENT_GRID, result_string);
				return 0;
			}

			if(volt_g < -5.0 || volt_g > 5.0 || volt_l12 < 360.0 || volt_l12 > 440.0){
				prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_GRID, "grid detect voltages out of range");
				prodtest_send(TEST_STATE_FAILURE, TEST_ITEM_COMPONENT_GRID, "Grid detect");
				return -1;
			}

			prodtest_send(TEST_STATE_SUCCESS, TEST_ITEM_COMPONENT_GRID, result_string);
			return 0;
		}else{
			prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_COMPONENT_GRID, "Grid detect data not received");
			prodtest_send(TEST_STATE_FAILURE, TEST_ITEM_COMPONENT_GRID, "Grid detect");
			return -1;
		}
	}

	return -1;
}



int run_component_tests(){
	ESP_LOGI(TAG, "testing components");

	if(test_speed_hwid()<0){
		goto err;
	}

	if(test_power_hwid()<0){
		goto err;
	}

	if(test_switch()<0){
		goto err;
	}

	if(test_bg()<0){
		goto err;
	}

	if(test_leds()<0){
		goto err;
	}

	if(test_buzzer()<0){
		goto err;
	}

	if(test_proximity()<0){
		goto err;
	}

#ifdef CONFIG_ZAPTEC_BUILD_TYPE_PRODUCTION
	if(test_efuses()<0){ // Will fail if security features are off
		goto err;
	}
#endif /* CONFIG_ZAPTEC_BUILD_TYPE_PRODUCTION */

	if(test_rtc()<0){
		goto err;
	}

	if(test_servo()<0){
		goto err;
	}

	if(test_hw_trig()<0){
		goto err;
	}

	if(test_grid_open()<0){
		goto err;
	}

	if(IsUKOPENPowerBoardRevision())
	{
		if(test_OPEN_relay()<0){
			goto err;
		}
	}

	return 0;

	err:
		return -1;
}


int charge_cycle_test(){

	//prodtest_send(TEST_STATE_RUNNING, TEST_ITEM_CHARGE_CYCLE, "Charge cycle");

	if(check_dspic_warnings(TEST_ITEM_INFO)<0){
		return -1;
	}

	char payload[100];

	ESP_LOGI(TAG, "charging started, sampling data"); 
	set_prodtest_led_state(TEST_STATE_RUNNING);
	float emeter_temps[] = {MCU_GetEmeterTemperature(0), MCU_GetEmeterTemperature(1), MCU_GetEmeterTemperature(2)};
	float emeter_voltages[] = { MCU_GetVoltages(0), MCU_GetVoltages(1), MCU_GetVoltages(2)};
	float emeter_currents[] = { MCU_GetCurrents(0), MCU_GetCurrents(1), MCU_GetCurrents(2)};
	float board_temps[] = {MCU_GetTemperaturePowerBoard(0), MCU_GetTemperaturePowerBoard(1)};

	prodtest_send(TEST_STATE_RUNNING, TEST_ITEM_CHARGE_CYCLE_EMETER_TEMPS, "eMeter temperature");

	float temperature_min = 1.0; 
	float temperature_max = 80.0;

	float volt_min = -1.0; 
	float volt_max = 50.0;

	float current_min = -1.0;
	float current_max = 5.0;

	if(IsUKOPENPowerBoardRevision() || onePhaseTest)
	{
		/// Temperatures - 1 phase
		sprintf(payload, "Emeter temp: %f", emeter_temps[0]);
		prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_CHARGE_CYCLE_EMETER_TEMPS, payload );

		if(emeter_temps[0] < temperature_min || emeter_temps[0] > temperature_max){
			prodtest_send(TEST_STATE_FAILURE, TEST_ITEM_CHARGE_CYCLE_EMETER_TEMPS, "eMeter temperature");
			return -1;
		}else{
			prodtest_send(TEST_STATE_SUCCESS, TEST_ITEM_CHARGE_CYCLE_EMETER_TEMPS, "eMeter temperature");
		}

		/// Voltages - 1 phase
		prodtest_send(TEST_STATE_RUNNING, TEST_ITEM_CHARGE_CYCLE_EMETER_VOLTAGES, "eMeter voltage before charging");
		sprintf(payload, "Emeter voltage before charging: %f", emeter_voltages[0]);
		prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_CHARGE_CYCLE_EMETER_VOLTAGES, payload );

		if(emeter_voltages[0] < volt_min || emeter_voltages[0] > volt_max){
			prodtest_send(TEST_STATE_FAILURE, TEST_ITEM_CHARGE_CYCLE_EMETER_VOLTAGES, "eMeter voltage before charging");
			return -1;
		}else{
			prodtest_send(TEST_STATE_SUCCESS, TEST_ITEM_CHARGE_CYCLE_EMETER_VOLTAGES, "eMeter voltage before charging");
		}

		/// Currents - 1 phase
		prodtest_send(TEST_STATE_RUNNING, TEST_ITEM_CHARGE_CYCLE_EMETER_CURRENTS, "eMeter current before charging");
		sprintf(payload, "Emeter current: %f", emeter_currents[0]);
		prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_CHARGE_CYCLE_EMETER_CURRENTS, payload );

		if(emeter_currents[0] < current_min	|| emeter_currents[0] > current_max){
			prodtest_send(TEST_STATE_FAILURE, TEST_ITEM_CHARGE_CYCLE_EMETER_CURRENTS, "eMeter current before charging");
			return -1;
		}else{
			prodtest_send(TEST_STATE_SUCCESS, TEST_ITEM_CHARGE_CYCLE_EMETER_CURRENTS, "eMeter current before charging");
		}

	}
	else
	{
		/// Temperatures - 3 phase
		sprintf(payload, "Emeter temps: %f, %f, %f", emeter_temps[0], emeter_temps[1], emeter_temps[2]);
		prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_CHARGE_CYCLE_EMETER_TEMPS, payload );

		if(emeter_temps[0] < temperature_min || emeter_temps[1]  < temperature_min || emeter_temps[2] < temperature_min
		|| emeter_temps[0] > temperature_max || emeter_temps[1] >  temperature_max || emeter_temps[2] > temperature_max){
			prodtest_send(TEST_STATE_FAILURE, TEST_ITEM_CHARGE_CYCLE_EMETER_TEMPS, "eMeter temps");
			return -1;
		}else{
			prodtest_send(TEST_STATE_SUCCESS, TEST_ITEM_CHARGE_CYCLE_EMETER_TEMPS, "eMeter temps");
		}

		/// Voltages - 3 phase
		prodtest_send(TEST_STATE_RUNNING, TEST_ITEM_CHARGE_CYCLE_EMETER_VOLTAGES, "eMeter voltages");
		sprintf(payload, "Emeter voltages before charging: %f, %f, %f", emeter_voltages[0], emeter_voltages[1], emeter_voltages[2]);
		prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_CHARGE_CYCLE_EMETER_VOLTAGES, payload );

		if(emeter_voltages[0] < volt_min || emeter_voltages[1]  < volt_min || emeter_voltages[2] < volt_min
		|| emeter_voltages[0] > volt_max || emeter_voltages[1] >  volt_max || emeter_voltages[2] > volt_max){
			prodtest_send(TEST_STATE_FAILURE, TEST_ITEM_CHARGE_CYCLE_EMETER_VOLTAGES, "eMeter voltages before charging");
			return -1;
		}else{
			prodtest_send(TEST_STATE_SUCCESS, TEST_ITEM_CHARGE_CYCLE_EMETER_VOLTAGES, "eMeter voltages before charging");
		}

		/// Currents - 3 phase
		prodtest_send(TEST_STATE_RUNNING, TEST_ITEM_CHARGE_CYCLE_EMETER_CURRENTS, "eMeter currents before charging");
		sprintf(payload, "Emeter currents before charging: %f, %f, %f", emeter_currents[0], emeter_currents[1], emeter_currents[2]);
		prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_CHARGE_CYCLE_EMETER_CURRENTS, payload );

		if(emeter_currents[0] < current_min || emeter_currents[1]  < current_min || emeter_currents[2] < current_min
		|| emeter_currents[0] > current_max || emeter_currents[1] >  current_max || emeter_currents[2] > current_max){
			prodtest_send(TEST_STATE_FAILURE, TEST_ITEM_CHARGE_CYCLE_EMETER_CURRENTS, "eMeter currents before charging");
			return -1;
		}else{
			prodtest_send(TEST_STATE_SUCCESS, TEST_ITEM_CHARGE_CYCLE_EMETER_CURRENTS, "eMeter currents before charging");
		}

	}


	prodtest_send(TEST_STATE_RUNNING, TEST_ITEM_CHARGE_CYCLE_OTHER_TEMPS, "Board temperatures");
	sprintf(payload, "Board temperatures: %f, %f", board_temps[0], board_temps[1]);
	prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_CHARGE_CYCLE_OTHER_TEMPS, payload );
	if(board_temps[0] < temperature_min || board_temps[1]  < temperature_min 
	|| board_temps[0] > temperature_max || board_temps[1] >  temperature_max){
		prodtest_send(TEST_STATE_FAILURE, TEST_ITEM_CHARGE_CYCLE_OTHER_TEMPS, "Board temperatures");
		return -1;
	}else{
		prodtest_send(TEST_STATE_SUCCESS, TEST_ITEM_CHARGE_CYCLE_OTHER_TEMPS, "Board temperatures");
	}

	ESP_LOGI(TAG, "Pre charging data:");
	ESP_LOGI(TAG, "\teMeter temp: %f, %f, %f", MCU_GetEmeterTemperature(0), MCU_GetEmeterTemperature(1), MCU_GetEmeterTemperature(2));
	ESP_LOGI(TAG, "\tVoltages: %f, %f, %f", MCU_GetVoltages(0), MCU_GetVoltages(1), MCU_GetVoltages(2));
	ESP_LOGI(TAG, "\tCurrents: %f, %f, %f", MCU_GetCurrents(0), MCU_GetCurrents(1), MCU_GetCurrents(2));
	ESP_LOGI(TAG, "\tOther temps: %f, %f", MCU_GetTemperaturePowerBoard(0), MCU_GetTemperaturePowerBoard(1));

	prodtest_send(TEST_STATE_RUNNING, TEST_ITEM_CHARGE_CYCLE_START, "Charge cycle start");

	if(check_dspic_warnings(TEST_ITEM_CHARGE_CYCLE_START)<0){
		return -1;
	}

	if(MCU_GetChargeMode()!=eCAR_DISCONNECTED){
		prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_CHARGE_CYCLE_START, "Handle connected to early");
		prodtest_send(TEST_STATE_FAILURE, TEST_ITEM_CHARGE_CYCLE_START, "Charge cycle start");
		return -1;
	}
	/*MessageType ret = MCU_SendCommandId(CommandServoClearCalibration);
	if(ret != MsgCommandAck){
		prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_CHARGE_CYCLE, "Calibration command send failed");
		prodtest_send(TEST_STATE_FAILURE, TEST_ITEM_CHARGE_CYCLE, "Charge cycle");
		return -1;
	}
	vTaskDelay(pdMS_TO_TICKS(10000));
	if(MCU_GetChargeMode()!=eCAR_DISCONNECTED){
		prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_CHARGE_CYCLE, "Handle connected while calibrating servo");
		prodtest_send(TEST_STATE_FAILURE, TEST_ITEM_CHARGE_CYCLE, "Charge cycle");
		return -1;
	}

	prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_CHARGE_CYCLE, "Servo calibrated");*/

	if(IsUKOPENPowerBoardRevision())
		prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_CHARGE_CYCLE_START, "Start charging");
	else
		prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_CHARGE_CYCLE_START, "Waiting for handle connect and charging start");

	ESP_LOGI(TAG, "waiting for charging start");
	set_prodtest_led_state(TEST_STAGE_WAITING_ANWER);
	while(MCU_GetChargeMode()!=eCAR_CHARGING){
		ESP_LOGI(TAG, "waiting for charging start");

		vTaskDelay(pdMS_TO_TICKS(1500));
	}

	prodtest_send(TEST_STATE_QUESTION, TEST_ITEM_CHARGE_CYCLE_START, "Handle locked?|Yes|No");
	int locked_result = await_prodtest_external_step_acceptance("Yes", true);
	if(locked_result != 0){
		prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_CHARGE_CYCLE_START, "Operator rejected lock");
		prodtest_send(TEST_STATE_FAILURE, TEST_ITEM_CHARGE_CYCLE_START, "Charge cycle start");
		return -1;
	}

	prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_CHARGE_CYCLE_START, "Operator accepted lock");

	if(check_dspic_warnings(TEST_ITEM_CHARGE_CYCLE_START)<0){
		return -1;
	}

	prodtest_send(TEST_STATE_SUCCESS, TEST_ITEM_CHARGE_CYCLE_START, "Charge cycle start");


	float emeter_voltages2[] = { MCU_GetVoltages(0), MCU_GetVoltages(1), MCU_GetVoltages(2)};
	prodtest_send(TEST_STATE_RUNNING, TEST_ITEM_CHARGE_CYCLE_EMETER_VOLTAGES2, "eMeter voltages while charging");

	float volt_min2 = 200.0; 
	float volt_max2 = 260.0;
	


	float eMCompareVoltage = 0.0;
	float OpenCompareVoltage = 0.0;
	
#ifdef CONFIG_ZAPTEC_RUN_FACTORY_TESTS
	current_min = -1.0;
#endif

	if(IsUKOPENPowerBoardRevision() || onePhaseTest)
	{
		current_max = 9.5;
		current_min = 7.0;

		/// Voltages2 1-phase
		sprintf(payload, "Emeter voltages while charging: %f", emeter_voltages2[0]);
		prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_CHARGE_CYCLE_EMETER_VOLTAGES2, payload );

		if(emeter_voltages2[0] < volt_min2 || emeter_voltages2[0] > volt_max2){
			prodtest_send(TEST_STATE_FAILURE, TEST_ITEM_CHARGE_CYCLE_EMETER_VOLTAGES2, "eMeter voltages while charging");
			return -1;
		}else{
			prodtest_send(TEST_STATE_SUCCESS, TEST_ITEM_CHARGE_CYCLE_EMETER_VOLTAGES2, "eMeter voltages while charging");
		}

		/// Current 1-phase
		prodtest_send(TEST_STATE_RUNNING, TEST_ITEM_CHARGE_CYCLE_EMETER_CURRENTS2, "Charge currents while charging");
		prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_CHARGE_CYCLE_EMETER_CURRENTS2, "Sampling charge cycle data" );

		for(int i = 0; i<10; i++){
			if(MCU_GetChargeMode()!=eCAR_CHARGING){
				prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_CHARGE_CYCLE_EMETER_CURRENTS2, "stop in charge cycle");
				prodtest_send(TEST_STATE_FAILURE, TEST_ITEM_CHARGE_CYCLE_EMETER_CURRENTS2, "Charge currents while charging");
				return -1;
			}

			snprintf(payload, 100, "Cycle currents[%d]: %f, %.2f", i, MCU_GetCurrents(0), GetPowerMeas());
			prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_CHARGE_CYCLE_EMETER_CURRENTS2, payload);

			if(i==5){
				eMCompareVoltage = MCU_GetVoltages(0);
				OpenCompareVoltage = MCU_GetOPENVoltage();

				if(MCU_GetCurrents(0)<current_min || MCU_GetCurrents(0) > current_max){
					prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_CHARGE_CYCLE_EMETER_CURRENTS2, "current out of range");
					prodtest_send(TEST_STATE_FAILURE, TEST_ITEM_CHARGE_CYCLE_EMETER_CURRENTS2, "Charge currents while charging");
					return -1;
				}
			}

			vTaskDelay(pdMS_TO_TICKS(1000));
		}

		///Compare the eMeter L1 vs the O-PEN measurements, sampled in the middle of the load test
		///Output the values and fail if not within limit.
		float vDiff = eMCompareVoltage - OpenCompareVoltage;
		if(vDiff < 0.0)
			vDiff *= -1.0;

		snprintf(payload, 100, "eM: %.2f V, O-PEN: %.2f V, Diff: %.2f V",  eMCompareVoltage, OpenCompareVoltage, vDiff);
		prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_CHARGE_CYCLE_EMETER_CURRENTS2, payload);
	#ifndef CONFIG_ZAPTEC_RUN_FACTORY_TESTS
		if(vDiff >= 3.0)
		{
			prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_CHARGE_CYCLE_EMETER_CURRENTS2, "Too high voltage difference");
			prodtest_send(TEST_STATE_FAILURE, TEST_ITEM_CHARGE_CYCLE_EMETER_CURRENTS2, "Too high voltage difference");
			return -1;
		}
	#endif

	}
	else
	{
		current_max = 8.0;
		current_min = 6.5;

		/// Voltages2 3-phase
		sprintf(payload, "Emeter voltages while charging: %f, %f, %f", emeter_voltages2[0], emeter_voltages2[1], emeter_voltages2[2]);
		prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_CHARGE_CYCLE_EMETER_VOLTAGES2, payload );

		if(
			   (emeter_voltages2[0] < volt_min2 || emeter_voltages2[0] > volt_max2)
			|| (emeter_voltages2[1] < volt_min2 || emeter_voltages2[1] > volt_max2)
			|| (emeter_voltages2[2] < volt_min2 || emeter_voltages2[2] > volt_max2)
			 ){
			prodtest_send(TEST_STATE_FAILURE, TEST_ITEM_CHARGE_CYCLE_EMETER_VOLTAGES2, "eMeter voltages while charging");
			return -1;
		}else{
			prodtest_send(TEST_STATE_SUCCESS, TEST_ITEM_CHARGE_CYCLE_EMETER_VOLTAGES2, "eMeter voltages while charging");
		}

		/// Current 3-phase
		prodtest_send(TEST_STATE_RUNNING, TEST_ITEM_CHARGE_CYCLE_EMETER_CURRENTS2, "Charge currents while charging");
		prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_CHARGE_CYCLE_EMETER_CURRENTS2, "Sampling charge currents" );

		for(int i = 0; i<10; i++){
			if(MCU_GetChargeMode()!=eCAR_CHARGING){
				prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_CHARGE_CYCLE_EMETER_CURRENTS2, "Stop in charge cycle");
				prodtest_send(TEST_STATE_FAILURE, TEST_ITEM_CHARGE_CYCLE_EMETER_CURRENTS2, "Charge currents while charging");
				return -1;
			}

			snprintf(payload, 100, "Cycle currents[%d]: %f, %f, %f, %.2f",
				 i, MCU_GetCurrents(0), MCU_GetCurrents(1), MCU_GetCurrents(2), GetPowerMeas()
			);
			prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_CHARGE_CYCLE_EMETER_CURRENTS2, payload);

			if(i==5){
				if(
					(MCU_GetCurrents(0)<current_min || MCU_GetCurrents(0) > current_max)
				 || (MCU_GetCurrents(1)<current_min || MCU_GetCurrents(1) > current_max)
				 || (MCU_GetCurrents(2)<current_min || MCU_GetCurrents(2) > current_max)
				){
					prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_CHARGE_CYCLE_EMETER_CURRENTS2, "Current out of range");
					prodtest_send(TEST_STATE_FAILURE, TEST_ITEM_CHARGE_CYCLE_EMETER_CURRENTS2, "Charge currents while charging");
					return -1;
				}
			}

			vTaskDelay(pdMS_TO_TICKS(1000));
		}
	}


	prodtest_send(TEST_STATE_MESSAGE, TEST_ITEM_CHARGE_CYCLE_EMETER_CURRENTS2, "Waiting for handle disconnect");

	while(MCU_GetChargeMode()!=eCAR_DISCONNECTED){
		vTaskDelay(pdMS_TO_TICKS(1000));
	}

	if(check_dspic_warnings(TEST_ITEM_CHARGE_CYCLE_EMETER_CURRENTS2)<0){
		return -1;
	}

	prodtest_send(TEST_STATE_SUCCESS, TEST_ITEM_CHARGE_CYCLE_EMETER_CURRENTS2, "Charge currents while charging");

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
	{29, "WARNING_SERVO"},
};

int check_dspic_warnings(enum test_item testItem)
{                    

	uint32_t warnings = MCU_GetWarnings();

	if(warnings == 0){
		//prodtest_send(TEST_STATE_MESSAGE, testItem, "No errors on dspic");
		return 0;
	}

	char payload[100];

	sprintf(payload, "warning mask: 0x%x", warnings);
	prodtest_send(TEST_STATE_MESSAGE, testItem, payload );

	uint8_t i = 0;
	for(i = 0; i<32; i++) {
		if((warnings & (1l<<i)) != 0) {
			uint8_t t;
			for(t = 0; t<(sizeof(dspic_warning_names) / sizeof(dspic_warning_names[0])); t++) {
				if(i == dspic_warning_names[t].bit) {
	                sprintf(payload, "dsPIC warning: %s (error code: %d <> %d)", dspic_warning_names[t].name, i, t);
					prodtest_send(TEST_STATE_MESSAGE, testItem, payload);
					break;
				}
			}
			
			if(t == (sizeof(dspic_warning_names) / sizeof(dspic_warning_names[0]))){
				sprintf(payload, "dsPIC warning: NAME NOT DEFINED (error code: %d <> %d)", i, t);
				prodtest_send(TEST_STATE_MESSAGE, testItem, payload);
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
			err = setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*) &tv, sizeof(struct timeval));
			if (err != 0) {
					ESP_LOGE(TAG, "Socket unable to connect: errno %d", errno);
					vTaskDelay(pdMS_TO_TICKS(3000));
					continue;
			}

			break;
    	}
}
