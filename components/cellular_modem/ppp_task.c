#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <string.h> 

#include "esp_log.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_netif.h"
#include "esp_netif_ppp.h"
#include "esp_event.h"

#include "ppp_task.h"
#include "at_commands.h"
#include "../../main/storage.h"

static const char *TAG = "PPP_TASK       ";


//#define CELLULAR_RX_SIZE 256 * 4 * 3
#define CELLULAR_RX_SIZE 5744*2 // Default TCP receive window size is 5744
#define CELLULAR_TX_SIZE 1024
#define CELLULAR_QUEUE_SIZE 40
#define CELLULAR_PIN_TXD  (GPIO_NUM_5)
#define CELLULAR_PIN_RXD  (GPIO_NUM_36)
#define CELLULAR_PIN_RTS  (GPIO_NUM_32)
#define CELLULAR_PIN_CTS  (GPIO_NUM_35)
#define CELLULAR_PIN_DTR  (GPIO_NUM_27)
#define RD_BUF_SIZE 256

#define GPIO_OUTPUT_PIN_SEL (1ULL<<GPIO_OUTPUT_PWRKEY | 1ULL<<GPIO_OUTPUT_RESET | 1ULL<<CELLULAR_PIN_DTR)
//#define GPIO_OUTPUT_PIN_SEL (1ULL<<GPIO_OUTPUT_PWRKEY | 1ULL<<GPIO_OUTPUT_DTR | 1ULL<<GPIO_OUTPUT_RESET)

static QueueHandle_t uart_queue;
static QueueHandle_t line_queue;
static char line_buffer[LINE_BUFFER_SIZE];
static uint32_t line_buffer_end;

#define LINE_QUEUE_LENGTH 3

static EventGroupHandle_t event_group;
static const int CONNECT_BIT = BIT0;
static const int UART_TO_PPP = BIT7;
static const int UART_TO_LINES = BIT6;

esp_netif_t *ppp_netif = NULL;
// esp_event_loop_handle_t ppp_netif_management_event_loop;

static bool hasLTEConnection = false;

static char modemName[20] 		= {0};
static char modemImei[20] 		= {0};
static char modemIccid[30]		= {0};
static char modemImsi[20]		= {0};
static char modemOperator[30]	= {0};

char pppIp4Address[16] = {0};

ESP_EVENT_DEFINE_BASE(ESP_MODEM_EVENT);
typedef enum {
    ESP_MODEM_EVENT_PPP_START = 0,       /*!< ESP Modem Start PPP Session */
    ESP_MODEM_EVENT_PPP_STOP  = 3,       /*!< ESP Modem Stop PPP Session*/
    ESP_MODEM_EVENT_UNKNOWN   = 4        /*!< ESP Modem Unknown Response */
} esp_modem_event_t;

void cellularPinsInit(void){
    gpio_config_t io_conf; 
	io_conf.intr_type = GPIO_PIN_INTR_DISABLE;
	io_conf.mode = GPIO_MODE_OUTPUT;
	io_conf.pin_bit_mask = GPIO_OUTPUT_PIN_SEL;
	io_conf.pull_down_en = 0;
	io_conf.pull_up_en = 0;
	gpio_config(&io_conf);

    gpio_set_level(GPIO_OUTPUT_RESET, 0);	//Low - Ensure off
    gpio_set_level(GPIO_OUTPUT_PWRKEY, 0);

    gpio_set_level(CELLULAR_PIN_DTR, 1);
}

void cellularPinsOn()
{
	  //BG95 power on sequence
	    //gpio_set_level(GPIO_OUTPUT_DTR, 0);//1
		ESP_LOGI(TAG, "BG ON");

	    gpio_set_level(GPIO_OUTPUT_RESET, 0);	//Low - Ensure off
	    gpio_set_level(GPIO_OUTPUT_PWRKEY, 0);
	    vTaskDelay(3000 / portTICK_PERIOD_MS);

	//    gpio_set_level(GPIO_OUTPUT_RESET, 0);	//High >= 30 ms
	//    gpio_set_level(GPIO_OUTPUT_PWRKEY, 0);
	//    vTaskDelay(200 / portTICK_PERIOD_MS);

	    gpio_set_level(GPIO_OUTPUT_RESET, 1);	//Low 1000 > x > 500 ms
	    gpio_set_level(GPIO_OUTPUT_PWRKEY, 1);
	    vTaskDelay(750 / portTICK_PERIOD_MS);

	    gpio_set_level(GPIO_OUTPUT_RESET, 0); 	//Keep high = ON
	    gpio_set_level(GPIO_OUTPUT_PWRKEY, 0);

	    vTaskDelay(3000 / portTICK_PERIOD_MS); //Delay to ensure it is ready
}

void cellularPinsOff()
{
	ESP_LOGI(TAG, "BG Toggle");

    gpio_set_level(GPIO_OUTPUT_RESET, 1);	//Low 1500 > x > 650 ms
    gpio_set_level(GPIO_OUTPUT_PWRKEY, 1);
    vTaskDelay(1000 / portTICK_PERIOD_MS);

    gpio_set_level(GPIO_OUTPUT_RESET, 0); 	//Keep high = OFF
    gpio_set_level(GPIO_OUTPUT_PWRKEY, 0);
}

void hard_reset_cellular(void){


    ESP_LOGI(TAG, "BG reset start 1");

    // NOTE: Pins are connected through transistors
    // causing output level to be inverted!!!

    //BG95 power on sequence
    //gpio_set_level(GPIO_OUTPUT_DTR, 0);//1

    gpio_set_level(GPIO_OUTPUT_RESET, 0);	//Low - Ensure off
    gpio_set_level(GPIO_OUTPUT_PWRKEY, 0);
    vTaskDelay(3000 / portTICK_PERIOD_MS);

//    gpio_set_level(GPIO_OUTPUT_RESET, 0);	//High >= 30 ms
//    gpio_set_level(GPIO_OUTPUT_PWRKEY, 0);
//    vTaskDelay(200 / portTICK_PERIOD_MS);

    gpio_set_level(GPIO_OUTPUT_RESET, 1);	//Low 1000 > x > 500 ms
    gpio_set_level(GPIO_OUTPUT_PWRKEY, 1);
    vTaskDelay(750 / portTICK_PERIOD_MS);

    gpio_set_level(GPIO_OUTPUT_RESET, 0); 	//Keep high = ON
    gpio_set_level(GPIO_OUTPUT_PWRKEY, 0);


    /*
    //BG96(!) power on sequence
    gpio_set_level(GPIO_OUTPUT_DTR, 1);

    gpio_set_level(GPIO_OUTPUT_PWRKEY, 1);
    gpio_set_level(GPIO_OUTPUT_RESET, 1);
    vTaskDelay(1000 / portTICK_PERIOD_MS);

    gpio_set_level(GPIO_OUTPUT_RESET, 0);

    //High >= 30 ms
	gpio_set_level(GPIO_OUTPUT_PWRKEY, 0);
    vTaskDelay(500 / portTICK_PERIOD_MS);

    //Low >= 500 ms
    gpio_set_level(GPIO_OUTPUT_PWRKEY, 1);
    vTaskDelay(500 / portTICK_PERIOD_MS);

    //Keep high
    gpio_set_level(GPIO_OUTPUT_PWRKEY, 0);
    */


	/*
	//Old working sequence
	gpio_set_level(GPIO_OUTPUT_RESET, 1);
	gpio_set_level(GPIO_OUTPUT_PWRKEY, 1);
	vTaskDelay(2000 / portTICK_PERIOD_MS);
	gpio_set_level(GPIO_OUTPUT_RESET, 0);
	vTaskDelay(10 / portTICK_PERIOD_MS);
    gpio_set_level(GPIO_OUTPUT_PWRKEY, 0);*/

	/*vTaskDelay(200 / portTICK_PERIOD_MS);
	gpio_set_level(GPIO_OUTPUT_PWRKEY, 1);
	vTaskDelay(1000 / portTICK_PERIOD_MS);
	gpio_set_level(GPIO_OUTPUT_PWRKEY, 0);
	vTaskDelay(1000 / portTICK_PERIOD_MS);*/

    ESP_LOGI(TAG, "BG reset done");
}

BaseType_t await_line(char *pvBuffer, TickType_t xTicksToWait){
    return xQueueReceive(line_queue, pvBuffer, xTicksToWait);
}

int send_line(char * line){
    uint32_t len = strlen(line);
    configASSERT(len<1024);
    uart_write_bytes(UART_NUM_1, line, len);
    uart_write_bytes(UART_NUM_1, "\r", 1);
    return 0;
}

void ppp_configure_uart(void){

    ESP_LOGI(TAG, "creating queue with elems size %d", sizeof( line_buffer ));
    line_queue = xQueueCreate( LINE_QUEUE_LENGTH, sizeof( line_buffer ) );
    if( line_queue == 0){
        ESP_LOGE(TAG, "failed to create line queue");
    }

    uart_config_t uart_config = {
        .baud_rate = 921600,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        // .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
        .flow_ctrl = UART_HW_FLOWCTRL_CTS_RTS,
        .rx_flow_ctrl_thresh = 120,
    };
    uart_param_config(UART_NUM_1, &uart_config);
    uart_set_pin(UART_NUM_1, CELLULAR_PIN_TXD, CELLULAR_PIN_RXD, CELLULAR_PIN_RTS, CELLULAR_PIN_CTS);
    uart_driver_install(
        UART_NUM_1, CELLULAR_RX_SIZE, CELLULAR_TX_SIZE,
        CELLULAR_QUEUE_SIZE, &uart_queue, 0
    );
    line_buffer_end = 0;

}

static void update_line_buffer(uint8_t* event_data,size_t size){
    event_data[size] = 0;
    // ESP_LOGI(TAG, "got uart data[%s]", event_data);

    if(size+line_buffer_end+1> LINE_BUFFER_SIZE){
        ESP_LOGE(TAG, "no space in line buffer! dropping data");
        line_buffer_end = 0;
        return;
    }

    for(int i=0; i<size; i++){
        char c = event_data[i];
        if((c=='\n')|| (c=='\r')){
            if(line_buffer_end == 0){
                // ESP_LOGD(TAG, "empty line");
            }else{
                // ESP_LOGD(TAG, "line finished");
                xQueueSend( line_queue, line_buffer, portMAX_DELAY);
                line_buffer_end = 0;
                ESP_LOGD(TAG, "Got line {%s}", line_buffer);
            }
            
        }else{
            line_buffer[line_buffer_end] = c;
            line_buffer_end++;
            line_buffer[line_buffer_end] = 0;
        }
    }

    ESP_LOGD(TAG, "current line buffer [%s]", line_buffer);

}

void clear_lines(void){
    ESP_LOGD(TAG, "clearing lines");
    int i = 0;
    while (uxQueueMessagesWaiting(line_queue) > 0){
        i++;
        xQueueReset(line_queue);
        // task blocked on queue submit may will be unblocked by xQueueReset
        // we keep resetting until the queue is empty
    }

    line_buffer_end = 0;
    line_buffer[0] = 0;

    if(i>1){
        ESP_LOGD(TAG, "Reseting the line queue used %d attempts", i);
    }
}

static void on_uart_data(uint8_t* event_data,size_t size){
    if(xEventGroupGetBits(event_group) & UART_TO_PPP){
        // ESP_LOGI(TAG, "passing uart data to ppp driver");
        esp_netif_receive(ppp_netif, event_data, size, NULL);
    }else if(xEventGroupGetBits(event_group) & UART_TO_LINES){
        update_line_buffer(event_data, size);
    }else{
        // we are transitioning between data and command mode
        // it should be safe to ignore data here, but lets log the event for now
        ESP_LOGD(TAG, "got uart data not passed to PPP or lines, len: %d", size);
    }
}

static TaskHandle_t eventTaskHandle = NULL;
int pppGetStackWatermark()
{
	if(eventTaskHandle != NULL)
		return uxTaskGetStackHighWaterMark(eventTaskHandle);
	else
		return -1;
}

static void uart_event_task(void *pvParameters)
{
    uart_event_t event;
    uint8_t* dtmp = (uint8_t*) malloc(RD_BUF_SIZE);
    for(;;) {
        //Waiting for UART event.
        if(xQueueReceive(uart_queue, (void * )&event, (portTickType)portMAX_DELAY)) {
            memset(dtmp, 0, RD_BUF_SIZE);
            //ESP_LOGI(TAG, "uart[%d] event:", UART_NUM_1);
            switch(event.type) {
                //Event of UART receving data
                /*We'd better handler data event fast, there would be much more data events than
                other types of events. If we take too much time on data event, the queue might
                be full.*/
                case UART_DATA:
                    //ESP_LOGI(TAG, "[UART DATA]: %d", event.size);
                    uart_read_bytes(UART_NUM_1, dtmp, event.size, portMAX_DELAY);
                    on_uart_data(dtmp, event.size);
                    // uart_write_bytes(UART_NUM_1, (const char*) dtmp, event.size);
                    break;
                //Event of HW FIFO overflow detected
                case UART_FIFO_OVF:
                    ESP_LOGE(TAG, "hw fifo overflow");
                    // If fifo overflow happened, you should consider adding flow control for your application.
                    // The ISR has already reset the rx FIFO,
                    // As an example, we directly flush the rx buffer here in order to read more data.
                    uart_flush_input(UART_NUM_1);
                    xQueueReset(uart_queue);
                    break;
                //Event of UART ring buffer full
                case UART_BUFFER_FULL:
                    ESP_LOGE(TAG, "ring buffer full");
                    // If buffer full happened, you should consider encreasing your buffer size
                    // As an example, we directly flush the rx buffer here in order to read more data.
                    uart_flush_input(UART_NUM_1);
                    xQueueReset(uart_queue);
                    break;
                //Event of UART RX break detected
                case UART_BREAK:
                    ESP_LOGE(TAG, "uart rx break");
                    break;
                //Event of UART parity check error
                case UART_PARITY_ERR:
                    ESP_LOGE(TAG, "uart parity error");
                    break;
                //Event of UART frame error
                case UART_FRAME_ERR:
                    ESP_LOGI(TAG, "uart frame error");
                    break;
                //Others
                default:
                    ESP_LOGI(TAG, "uart event type: %d", event.type);
                    break;
            }
        }
    }
    free(dtmp);
    dtmp = NULL;
    vTaskDelete(NULL);
}

int GetNumberAsString(char * inputString, char * outputString, int maxLength)
{
	volatile int index = 0;
	int inLength = strlen(inputString);

	for (int i = 0; i < inLength; i++)
	{
		if((0x39 >= inputString[i]) && (inputString[i] >= 0x30))
		{
			if(index > maxLength-2) //Stop before \0
			{
				ESP_LOGE(TAG, "Too long string");
				return -1;
			}

			outputString[index] = inputString[i];
			index++;
		}
	}
	return 0;
}

static uint8_t powerOnCount = 0;
int configure_modem_for_ppp(void){

    bool startup_confirmed = false;
    bool active_confirmed = false;
    char at_buffer[LINE_BUFFER_SIZE] = {0};
    
    //gpio_set_level(GPIO_OUTPUT_DTR, 1);


    enter_command_mode();
    at_command_echo_set(true);
    vTaskDelay(pdMS_TO_TICKS(2000));

    at_command_at();

    await_line(at_buffer, pdMS_TO_TICKS(1000));
    int timeout = 0;
    while((startup_confirmed == false) && (active_confirmed == false))
    {
    	if(strstr(at_buffer, "APP RDY"))
    	{
    		startup_confirmed = true;
    		ESP_LOGI(TAG, "BG startup confirmed");

            at_command_echo_set(false);

    	    vTaskDelay(pdMS_TO_TICKS(100));
    	    int at_result = at_command_at();
    	    while(at_result < 0){
    	        ESP_LOGE(TAG, "bad response from modem: %d, retrying ", at_result);
    	        at_result = at_command_at();
    	        vTaskDelay(pdMS_TO_TICKS(1000));

    	        ESP_LOGE(TAG, "AT result %d ", at_result);
    	    }

            vTaskDelay(pdMS_TO_TICKS(500)); 

    	}

    	else if(strstr(at_buffer, "OK"))
    	{
    		active_confirmed = true;
    		ESP_LOGI(TAG, "BG already started");

    	    //Make sure to clear receive buffer before reading device info
    	    memset(at_buffer,0,LINE_BUFFER_SIZE);
    	    await_line(at_buffer, pdMS_TO_TICKS(3000));
    	    ESP_LOGI(TAG, "Clearing line buffer %s", at_buffer);
    	    while (strlen(at_buffer) > 0)
    	    {
    			vTaskDelay(pdMS_TO_TICKS(1000));
    			memset(at_buffer,0,LINE_BUFFER_SIZE);
    			await_line(at_buffer, pdMS_TO_TICKS(1000));
    			ESP_LOGI(TAG, "Clearing line buffer %s", at_buffer);
    	    }
    	}

		else
		{
			ESP_LOGW(TAG, "Failed to get line: %d", timeout);
    		ESP_LOGI(TAG, "checking line %s", at_buffer);
    		memset(at_buffer,0,LINE_BUFFER_SIZE);
        	await_line(at_buffer, pdMS_TO_TICKS(1000));
        	ESP_LOGI(TAG, "Checking receive buffer %s", at_buffer);

        	if(strlen(at_buffer) == 0)
        		timeout++;
        	else
        		timeout = 0;
		}

        if((timeout == 3) && (startup_confirmed == false))
        {
        	ESP_LOGW(TAG, "Power toggling BG due to timeout");

        	xEventGroupClearBits(event_group, UART_TO_PPP);
			xEventGroupSetBits(event_group, UART_TO_LINES);
			clear_lines();
        	cellularPinsOn();
        	powerOnCount++;
        	if(powerOnCount >= 10)
        	{
        		ESP_LOGE(TAG, "NO BG BOOT DETECTED");
				storage_Set_And_Save_DiagnosticsLog("#13 No BG boot detected (10 attempts)");
				esp_restart();
        	}
        	timeout = 0;
        }
        //timeout++;
    }

    at_command_echo_set(false);

    if(at_command_flow_ctrl_enable()<0){
        ESP_LOGE(TAG, "Failed to enable flow control on cellular UART");
    }
    else
    {
    	ESP_LOGW(TAG, "Flow control on cellular UART enabled");
    }

    //Check if correct Band setting
    char response[100] = {0};
    at_command_get_LTE_band(response, 100);
    if(response != NULL)
    {
    	ESP_LOGI(TAG, "LTE Band: %s", response);

    	char * bandSet = strstr(response, ",0x8080084,");
    	if(bandSet == NULL)
    	{
    		ESP_LOGI(TAG, "Band not set, writing and soft restarting");

    	    int lteOK = at_command_set_LTE_M_only_at_boot();
    	    if(lteOK == 0)
    	    	ESP_LOGI(TAG, "Set to LTE-M only");
    	    else
    	    	ESP_LOGE(TAG, "Failed to set LTE-M only");


    	    int lteBandOK = at_command_set_LTE_band_at_boot();
    		if(lteBandOK == 0)
    			ESP_LOGI(TAG, "Set to LTE-M band");
    		else
    			ESP_LOGE(TAG, "Failed to set LTE-M band");

    		ESP_LOGW(TAG, "Soft restarting BG");
    		at_command_soft_restart();

    		vTaskDelay(pdMS_TO_TICKS(15000));
    	}
    }


    //Checking both CREG and Operator is redundant. Now just checking operator as before.

 /* CREG Definitions
 	0 Not registered. MT is not currently searching an operator to register to.
    1 Registered, home network.
    2 Not registered, but MT is currently trying to attach or searching an operator to
    3 Registration denied
    4 Unknown
    5 Registered, roaming
  */
    //Check for valid CREG response
    /*if(strstr(reply, "CREG"))
    {
    	//Check for valid CREG state
		char *cregHome = strstr(reply, ",1");
		char *cregRoam = strstr(reply, ",5");

		if(cregHome != NULL)
			ESP_LOGI(TAG, "cregHome %s", cregHome);

		if(cregRoam != NULL)
			ESP_LOGI(TAG, "cregRoam %s", cregRoam);

		while ((cregHome == NULL) && (cregRoam == NULL))
		{
			//Repeat checking for valid CREG state
			at_command_network_registration_status(reply);
			if(strstr(reply, "CREG"))
			{
				//Repeat checking for valid CREG response
				cregHome = strstr(reply, ",1");
				cregRoam = strstr(reply, ",5");

				if(cregHome != NULL)
				{
					ESP_LOGI(TAG, "cregHome %s", cregHome);
				}
				else if(cregRoam != NULL)
				{
					ESP_LOGI(TAG, "cregRoam %s", cregRoam);
				}
				else
				{
					ESP_LOGW(TAG, "No matching CREG values");
					vTaskDelay(pdMS_TO_TICKS(3000));
				}
			}
			else
			{
				ESP_LOGW(TAG, "No matching CREG label");
				vTaskDelay(pdMS_TO_TICKS(3000));
			}
		}
    }*/


    /*
    ESP_LOGI(TAG, "checking CREG");
    char reply[20] = {0};
    at_command_network_registration_status(reply);
    ESP_LOGI(TAG, "CREG: %s", reply);
    */

    int i;
    for (i = 0; i <= 3; i++)
    {
    	memset(at_buffer,0,LINE_BUFFER_SIZE);
    	await_line(at_buffer, pdMS_TO_TICKS(1000));
    	ESP_LOGI(TAG, "Clearing line buffer %s", at_buffer);
    }

    at_command_echo_set(false);

    //Detect BG9x-model string to ensure AT-commands are in sync before we continue
    char name[20];
    char *pName = NULL;
    uint8_t nTimeout = 10;
    while ((pName == NULL) && (nTimeout > 0))
    {
    	memset(name, 0, 20);
    	at_command_get_model_name(name, 20);
    	if(strlen(name) > 0)
    	{
    		pName = strstr(name, "BG9");
    		if(pName != NULL)
    		{
    			strcpy(modemName, name);
   			    ESP_LOGI(TAG, "got name %s", modemName);
    			break;
    		}
    		else
    		{
    			ESP_LOGE(TAG, "got name %s", modemName);
    		}
    	}

    	vTaskDelay(pdMS_TO_TICKS(2000));
    	nTimeout--;
    }

    char imei[20] = {0};
    at_command_get_imei(imei, 20);
    GetNumberAsString(imei, modemImei, 20);
    ESP_LOGI(TAG, "got imei %s", modemImei);

    char Iccid[30] = {0};
	at_command_get_ccid(Iccid, 30);
	GetNumberAsString(Iccid, modemIccid, 30);
	ESP_LOGI(TAG, "got Iccid %s", modemIccid);

    char imsi[20] = {0};
    at_command_get_imsi(imsi, 20);
    GetNumberAsString(imsi, modemImsi, 20);
    ESP_LOGI(TAG, "got imsi %s", modemImsi);


    char op[30] = {0};
    at_command_get_operator(op, 30);
    while (strlen(op) < 12)
    {
    	vTaskDelay(pdMS_TO_TICKS(3000));
    	at_command_get_operator(op, 30);
    }

    strcpy(modemOperator, op);
    ESP_LOGI(TAG, "got operator %s", modemOperator);


	ESP_LOGI(TAG, "dialing(?)");
	at_command_pdp_define();
	at_command_dial();
	enter_data_mode();
	if(active_confirmed == true)
		return 1;


    return 0;
}

static void on_ip_event(void *arg, esp_event_base_t event_base,
                      int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "IP event! %d", event_id);
    if (event_id == IP_EVENT_PPP_GOT_IP) {
        esp_netif_dns_info_t dns_info;

        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        esp_netif_t *netif = event->esp_netif;

        ESP_LOGI(TAG, "Modem Connect to PPP Server");
        ESP_LOGI(TAG, "~~~~~~~~~~~~~~");
        ESP_LOGI(TAG, "IP          : " IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Netmask     : " IPSTR, IP2STR(&event->ip_info.netmask));
        ESP_LOGI(TAG, "Gateway     : " IPSTR, IP2STR(&event->ip_info.gw));
        esp_netif_get_dns_info(netif, 0, &dns_info);
        ESP_LOGI(TAG, "Name Server1: " IPSTR, IP2STR(&dns_info.ip.u_addr.ip4));
        esp_netif_get_dns_info(netif, 1, &dns_info);
        ESP_LOGI(TAG, "Name Server2: " IPSTR, IP2STR(&dns_info.ip.u_addr.ip4));
        ESP_LOGI(TAG, "~~~~~~~~~~~~~~");
        xEventGroupSetBits(event_group, CONNECT_BIT);

        ESP_LOGI(TAG, "GOT ip event!!!");
        hasLTEConnection = true;
        sprintf(pppIp4Address,IPSTR, IP2STR(&event->ip_info.ip));

    } else if (event_id == IP_EVENT_PPP_LOST_IP) {
        ESP_LOGI(TAG, "Modem Disconnect from PPP Server");
        hasLTEConnection = false;
        strcpy(pppIp4Address, "0.0.0.0");
    } else if (event_id == IP_EVENT_GOT_IP6) {
        ESP_LOGI(TAG, "GOT IPv6 event!");

        ip_event_got_ip6_t *event = (ip_event_got_ip6_t *)event_data;
        ESP_LOGI(TAG, "Got IPv6 address " IPV6STR, IPV62STR(event->ip6_info.ip));
        hasLTEConnection = true;
    }
}


bool LteIsConnected()
{
	return hasLTEConnection;
}

char * pppGetIp4Address()
{
	return pppIp4Address;
}

static void on_ppp_changed(void *arg, esp_event_base_t event_base,
                        int32_t event_id, void *event_data)
{
    ESP_LOGI(TAG, "PPP state changed event %d", event_id);
    if (event_id == NETIF_PPP_ERRORUSER) {
        /* User interrupted event from esp-netif */
        esp_netif_t *netif = event_data;
        ESP_LOGW(TAG, "User interrupted event from netif:%p", netif);
    }
}

static esp_err_t send_ppp_bytes_to_uart(void *h, void *buffer, size_t len){
    if(!(xEventGroupGetBits(event_group) & UART_TO_PPP)){
        ESP_LOGW(TAG, "got bytes from ppp driver while in command mode, discarding");
        return ESP_FAIL;
    }

    //ESP_LOGI(TAG, "sending ppp data to modem");
    int sent_bytes = uart_write_bytes(UART_NUM_1, buffer, len);
    if(sent_bytes == len){
        return ESP_OK;
    }
    return ESP_FAIL;
}

static esp_err_t post_attach_cb(esp_netif_t * esp_netif, void * args)
{
    ESP_LOGI(TAG, "configuring uart transmit for PPP");
    const esp_netif_driver_ifconfig_t driver_ifconfig = {
            .driver_free_rx_buffer = NULL,
            .transmit = send_ppp_bytes_to_uart,
            .handle = NULL
    };

    esp_netif_set_driver_config(esp_netif, &driver_ifconfig);
    esp_event_post(ESP_MODEM_EVENT, ESP_MODEM_EVENT_PPP_START, NULL, 0, 0);
    return ESP_OK;
}

int enter_command_mode(void){
    ESP_LOGI(TAG, "Clearing out ppp");
    xEventGroupClearBits(event_group, UART_TO_PPP);

    int at_result = -10;
    int retries = 3;

    for(int retry = 0; retry < retries; retry++){
        vTaskDelay(pdMS_TO_TICKS(1000));
        ESP_LOGI(TAG, "sending command to exit ppp mode");
        uart_write_bytes(UART_NUM_1, "+++", 3);
        vTaskDelay(pdMS_TO_TICKS(1000));

        //we may get junk from the modem, wait the required time and test with at
        clear_lines();// clear any extra ppp data from the modem
        xEventGroupSetBits(event_group, UART_TO_LINES);

        at_command_echo_set(false);

        ESP_LOGD(TAG, "checking if ppp exit succeeded");
        at_result = at_command_at();

        if(at_result < 0){
            ESP_LOGW(
                TAG, "bad response from modem: %d, retry count %d",
                at_result, retry
            );
        }else{
            ESP_LOGI(TAG, "Command mode confirmed");
            return 0;
        }

    }

    ESP_LOGE(TAG, "Failed to enter command mode! Restoring PPP");
    xEventGroupClearBits(event_group, UART_TO_LINES);
    xEventGroupSetBits(event_group, UART_TO_PPP);
    return -1;
}

int enter_data_mode(void){
    ESP_LOGI(TAG, "going back to data mode");
    int mode_change_result = at_command_data_mode();

    if(mode_change_result == 0){
        ESP_LOGI(TAG, "Routing uart data to ppp driver");
        xEventGroupClearBits(event_group, UART_TO_LINES);
        xEventGroupSetBits(event_group, UART_TO_PPP);
        return 0;
    }
    return -1;
}


esp_netif_driver_base_t *base_driver;
esp_event_handler_instance_t start_reg;

void ppp_task_start(void){
    event_group = xEventGroupCreate();
    ESP_LOGI(TAG, "Configuring BG9x");
    xEventGroupSetBits(event_group, UART_TO_LINES);
    //hard_reset_cellular();
    //configure_uart();
    //ESP_LOGI(TAG, "uart configured");
    //configure_modem_for_ppp(); // TODO rename

    xTaskCreate(uart_event_task, "uart_event_task", 5000, NULL, 7, &eventTaskHandle);

    xEventGroupClearBits(event_group, UART_TO_PPP);
    xEventGroupSetBits(event_group, UART_TO_LINES);

    int connectionStatus = configure_modem_for_ppp(); // TODO rename

    esp_netif_init();
    esp_event_loop_create_default();

    // Init netif object
    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_PPP();
    ppp_netif = esp_netif_new(&cfg);
    assert(ppp_netif);

    esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &on_ip_event, ppp_netif);
    esp_event_handler_register(NETIF_PPP_STATUS, ESP_EVENT_ANY_ID, &on_ppp_changed, ppp_netif);

    // Note: (Steve)
    //
    // In 4.4.1 there's a bug where `ppp_netif' doesn't get assigned as the
    // lwIP default interface because it's not in UP state, seems the esp_netif
    // code checks UP status with netif_is_link_up but calls only netif_set_up
    // and not netif_set_link_up!
    //
    // A quick fix is to add default handlers for connection/disconnection which
    // seems to set the default interface and subsequently brings the interface into
    // UP state as well.
    //
    // This can probably be removed for ESP-IDF 5.0 as they seem to handle the link
    // separately from the interface.
    esp_event_handler_register(IP_EVENT, IP_EVENT_PPP_GOT_IP, esp_netif_action_connected, ppp_netif);
    esp_event_handler_register(IP_EVENT, IP_EVENT_PPP_LOST_IP, esp_netif_action_disconnected, ppp_netif);

    if(connectionStatus == 0)
    {
    	//Must make new connection
    	ESP_LOGI(TAG, "Running at_command_pdp_define");

    	at_command_pdp_define();
    	ESP_LOGI(TAG, "dialing");
    	at_command_dial();
    }

    //esp_netif_driver_base_t *base_driver = calloc(1, sizeof(esp_netif_driver_base_t));
    base_driver = calloc(1, sizeof(esp_netif_driver_base_t));
    base_driver->post_attach = &post_attach_cb;

    // do we need this one?
    //esp_modem_set_event_handler(dte, modem_event_handler, ESP_EVENT_ANY_ID, NULL);

    // toggle uart rx to go to modem_netif_receive_cb
    xEventGroupClearBits(event_group, UART_TO_LINES);
    xEventGroupSetBits(event_group, UART_TO_PPP);

    //esp_event_handler_instance_t start_reg;
    esp_event_handler_instance_register(
         ESP_MODEM_EVENT, ESP_MODEM_EVENT_PPP_START,
         esp_netif_action_start, ppp_netif,
         &start_reg
    );

    esp_netif_attach(ppp_netif, (void *)base_driver);

    // functions to use later:
    // esp_netif_action_stop
    // esp_netif_action_connected
    // esp_netif_action_disconnected
}

int configure_modem_for_prodtest(void (log_cb)(char *)){
    event_group = xEventGroupCreate();
    ESP_LOGI(TAG, "Configuring BG9x for prodtest");
    xEventGroupSetBits(event_group, UART_TO_LINES);
    xTaskCreate(uart_event_task, "uart_event_task", 2048*2, NULL, 7, &eventTaskHandle);

    xEventGroupClearBits(event_group, UART_TO_PPP);
    xEventGroupSetBits(event_group, UART_TO_LINES);

    bool baudrate_already_high = false;
    bool bg_started = false;

    log_cb("Checking BG95 state");
    
    while (true)
    {
        uart_set_baudrate( UART_NUM_1, 921600);
        clear_lines();
        for(int i = 0; i<3; i++){
            int fast_at_result = at_command_detect_echo();
            if(fast_at_result>0){
                baudrate_already_high = true;
                bg_started = true;
                break;
            }
        }

        if(bg_started)
            break;

        uart_set_baudrate( UART_NUM_1, 115200);
        clear_lines();
        for(int i = 0; i<5; i++){
            int at_result = at_command_detect_echo();
            if(at_result>0){
                bg_started = true;
                break;
            }
        }

        if(bg_started)
            break;

        log_cb("turning on BG95");
        cellularPinsOn();
        vTaskDelay(pdMS_TO_TICKS(3000));
    }

    clear_lines();
    at_command_echo_set(false);
    clear_lines();

    // testing for Kenneth
    // while (true)
    // {
    //     at_command_detect_echo();
    // }
    

    // //test only
    // at_command_set_baud_low();
    // uart_set_baudrate( UART_NUM_1, 115200);
    // baudrate_already_high = false;
    // clear_lines();
    // vTaskDelay(psMS_TO_TICKS(300));
    // // test end

    if(baudrate_already_high!=true){
        int modem_baud_set_error = at_command_set_baud_high();
        if(modem_baud_set_error != 0){
            ESP_LOGE(TAG, "failed to upgrade baud rate");
            return -1;
        }

        uart_set_baudrate( UART_NUM_1, 921600);
        log_cb("BG95 baudrate upgraded");
    }

    clear_lines();

    ESP_LOGI(TAG, "BG started");


    int at_result = at_command_at();

    while(at_result < 0){
        ESP_LOGE(TAG, "bad response from modem: %d, retrying ", at_result);
        vTaskDelay(pdMS_TO_TICKS(1000));
        at_result = at_command_at();
    }

    ESP_LOGI(TAG, "[BG] Go for prodtest");
    return 0;
}

int ppp_disconnect()
{
	hasLTEConnection = false;

	esp_event_handler_unregister(IP_EVENT, ESP_EVENT_ANY_ID, &on_ip_event);
	esp_event_handler_unregister(NETIF_PPP_STATUS, ESP_EVENT_ANY_ID, &on_ppp_changed);
	esp_event_loop_delete_default();

	//vTaskDelay(pdMS_TO_TICKS(500));
	//esp_netif_action_stop(ppp_netif, (void *)base_driver, ESP_MODEM_EVENT_PPP_STOP, &start_reg);//?
	esp_netif_destroy(ppp_netif);

	return 0;
}

void ATOnly()
{
	event_group = xEventGroupCreate();
	ESP_LOGI(TAG, "Configuring BG9x for prodtest");
	xEventGroupSetBits(event_group, UART_TO_LINES);
	xTaskCreate(uart_event_task, "uart_event_task", 2048*2, NULL, 7, &eventTaskHandle);

	xEventGroupClearBits(event_group, UART_TO_PPP);
	xEventGroupSetBits(event_group, UART_TO_LINES);

	uart_set_baudrate( UART_NUM_1, 921600);
	clear_lines();
}


static char stringBuffer[300];
static bool hasNewData = false;
int TunnelATCommand(char * unformattedCommand, bool changeMode){

	ClearATBuffer();

	clear_lines();

	char atCommand[100] = {0};

	if(strstr(unformattedCommand,"+++"))
	{
		strcpy(atCommand,"+++");
		changeMode = 0;
	}
	else
	{

		char * start = strstr(unformattedCommand,"[");

		int nextChar = 0;
		for (int i = 2; i < 98; i++)
		{
			if(start[i] != '\\')
			{
				atCommand[nextChar] = start[i];
				nextChar++;
			}
			if(start[i] == ']')
				break;
		}
		atCommand[nextChar-2] = '\0';
	}

	int enter_command_mode_result = 0;

	if(changeMode)
	{
		enter_command_mode_result = enter_command_mode();

		if(enter_command_mode_result<0){
			ESP_LOGW(TAG, "failed to enter command mode");
			vTaskDelay(pdMS_TO_TICKS(500));// wait to make sure all logs are flushed
		}
	}

	char response[150] = {0};
	at_command_generic(atCommand, response, 150);

	//char stringbuffer[300];
	sprintf(stringBuffer, "cmd: %s, res: %s", atCommand, response);
	ESP_LOGI(TAG, "AT tunnel: %s", stringBuffer );

	if(changeMode)
	{
		int enter_data_mode_result = enter_data_mode();
		ESP_LOGI(TAG, "at command poll:[%d];[%d];", enter_command_mode_result, enter_data_mode_result);
	}

	hasNewData = true;
	return 1;
}

bool HasNewData()
{
	return hasNewData;
}

char * GetATBuffer()
{
	return stringBuffer;
}

void ClearATBuffer()
{
	hasNewData = false;
	memset(stringBuffer, 0, 300);
}

static int rssiLTE_percent = 0;
int log_cellular_quality(void){

	int rssiLTE = 0;

	int enter_command_mode_result = enter_command_mode();

	if(enter_command_mode_result<0){
		ESP_LOGW(TAG, "failed to enter command mode, skiping rssi log");
		vTaskDelay(pdMS_TO_TICKS(500));// wait to make sure all logs are flushed
		return 0;
	}

	char sysmode[16]; int rssi; int rsrp; int sinr; int rsrq;
	at_command_signal_strength(sysmode, &rssi, &rsrp, &sinr, &rsrq);

	char signal_string[256];
	snprintf(signal_string, 256, "[AT+QCSQ Report Signal Strength] mode: %s, rssi: %d, rsrp: %d, sinr: %d, rsrq: %d", sysmode, rssi, rsrp, sinr, rsrq);
	ESP_LOGI(TAG, "sending diagnostics observation (1/2): \"%s\"", signal_string);
	//publish_diagnostics_observation(signal_string);

	int ber;
	char quality_string[256];
	at_command_signal_quality(&rssiLTE, &ber);
	snprintf(quality_string, 256, "[AT+CSQ Signal Quality Report] rssi: %d, ber: %d", rssiLTE, ber);
	ESP_LOGI(TAG, "sending diagnostics observation (2/2): \"%s\"", quality_string );
	//publish_diagnostics_observation(quality_string);

	int enter_data_mode_result = enter_data_mode();
	ESP_LOGI(TAG, "at command poll:[%d];[%d];", enter_command_mode_result, enter_data_mode_result);

	int rssiLTE_dbm = 2*rssiLTE - 113;

	//These are the level conversions used in Pro.
	if (rssiLTE_dbm > -55)
		rssiLTE_percent = 100;
	else if (rssiLTE_dbm > -65)
		rssiLTE_percent = 80;
	else if (rssiLTE_dbm > -75)
		rssiLTE_percent = 60;
	else if (rssiLTE_dbm > -85)
		rssiLTE_percent = 40;
	else if (rssiLTE_dbm > -95)
		rssiLTE_percent = 20;
	else if (rssiLTE_dbm > -105)
		rssiLTE_percent = 0;

	return rssiLTE_percent;
}

int GetCellularQuality()
{
	if(hasLTEConnection == false)
		return 0;
	else
		return rssiLTE_percent;
}

const char* LTEGetImei()
{
	return modemImei;
}
const char* LTEGetIccid()
{
	return modemIccid;
}
const char* LTEGetImsi()
{
	return modemImsi;
}
