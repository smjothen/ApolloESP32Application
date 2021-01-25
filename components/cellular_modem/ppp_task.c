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

static const char *TAG = "PPP_TASK";


#define CELLULAR_RX_SIZE 256 * 4 * 3
#define CELLULAR_RX_SIZE 5744*2 // Default TCP receive window size is 5744
#define CELLULAR_TX_SIZE 1024
#define CELLULAR_QUEUE_SIZE 40
#define CELLULAR_PIN_TXD  (GPIO_NUM_5)
#define CELLULAR_PIN_RXD  (GPIO_NUM_36)
#define CELLULAR_PIN_RTS  (GPIO_NUM_32)
#define CELLULAR_PIN_CTS  (GPIO_NUM_35)
#define RD_BUF_SIZE 256

#define GPIO_OUTPUT_PIN_SEL (1ULL<<GPIO_OUTPUT_PWRKEY | 1ULL<<GPIO_OUTPUT_RESET)
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
}

void cellularPinsOn()
{
	  //BG95 power on sequence
	    //gpio_set_level(GPIO_OUTPUT_DTR, 0);//1
		ESP_LOGI(TAG, "BG ON...");

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
	ESP_LOGI(TAG, "BG OFF...");

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

void configure_uart(void){

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
    //ESP_LOGI(TAG, "got uart data[%s]", event_data);

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
                //ESP_LOGI(TAG, "Got line {%s}", line_buffer);
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
    size_t buffered_size;
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
	int i;
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

            int echo_cmd_result = at_command_echo_set(false);

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
        	cellularPinsOn();
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

    char name[20];
    at_command_get_model_name(name, 20);
    strcpy(modemName, name);
    ESP_LOGI(TAG, "got name %s", modemName);

    char imei[20];
    at_command_get_imei(imei, 20);
    GetNumberAsString(imei, modemImei, 20);
    ESP_LOGI(TAG, "got imei %s", modemImei);

    char Iccid[30];
	at_command_get_ccid(Iccid, 30);
	GetNumberAsString(Iccid, modemIccid, 30);
	ESP_LOGI(TAG, "got Iccid %s", modemIccid);

    char imsi[20];
    at_command_get_imsi(imsi, 20);
    GetNumberAsString(imsi, modemImsi, 20);
    ESP_LOGI(TAG, "got imsi %s", modemImsi);

    char op[30];
    at_command_get_operator(op, 30);
    strcpy(modemOperator, op);
    ESP_LOGI(TAG, "got operator %s", modemOperator);

    ESP_LOGD(TAG, "checking CREG");
    at_command_network_registration_status();
    
    if(active_confirmed == true)
    {
	    enter_data_mode();
	    //hasLTEConnection = true;
	    return 1;
    }

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
    } else if (event_id == IP_EVENT_PPP_LOST_IP) {
        ESP_LOGI(TAG, "Modem Disconnect from PPP Server");
        hasLTEConnection = false;
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
    int retries = 5;

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

void ppp_task_start(void){
    event_group = xEventGroupCreate();
    ESP_LOGI(TAG, "Configuring BG9x");
    xEventGroupSetBits(event_group, UART_TO_LINES);
    //hard_reset_cellular();
    //configure_uart();
    //ESP_LOGI(TAG, "uart configured");
    //configure_modem_for_ppp(); // TODO rename

    xTaskCreate(uart_event_task, "uart_event_task", 2048, NULL, 7, &eventTaskHandle);
    //xTaskCreate(uart_event_task, "uart_event_task", 4096, NULL, 7, eventTaskHandle);

    xEventGroupClearBits(event_group, UART_TO_PPP);
    xEventGroupSetBits(event_group, UART_TO_LINES);

    int connectionStatus = configure_modem_for_ppp(); // TODO rename

    esp_netif_init();
    esp_event_loop_create_default();
    esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &on_ip_event, NULL);
    esp_event_handler_register(NETIF_PPP_STATUS, ESP_EVENT_ANY_ID, &on_ppp_changed, NULL);

    // Init netif object
    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_PPP();
    ppp_netif = esp_netif_new(&cfg);
    assert(ppp_netif);

    if(connectionStatus == 0)
    {
    	//Must make new connection
    	ESP_LOGI(TAG, "Running at_command_pdp_define");

    	at_command_pdp_define();
    	ESP_LOGI(TAG, "dialing");
    	at_command_dial();
    }

    esp_netif_driver_base_t *base_driver = calloc(1, sizeof(esp_netif_driver_base_t));
    base_driver->post_attach = &post_attach_cb;

    // do we need this one?
    //esp_modem_set_event_handler(dte, modem_event_handler, ESP_EVENT_ANY_ID, NULL);

    // toggle uart rx to go to modem_netif_receive_cb
    xEventGroupClearBits(event_group, UART_TO_LINES);
    xEventGroupSetBits(event_group, UART_TO_PPP);

    esp_event_handler_instance_t start_reg;
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

int configure_modem_for_prodtest(void){
    event_group = xEventGroupCreate();
    ESP_LOGI(TAG, "Configuring BG9x for prodtest");
    xEventGroupSetBits(event_group, UART_TO_LINES);
    hard_reset_cellular();
    xTaskCreate(uart_event_task, "uart_event_task", 2048, NULL, 7, &eventTaskHandle);

    xEventGroupClearBits(event_group, UART_TO_PPP);
    xEventGroupSetBits(event_group, UART_TO_LINES);

    char at_buffer[LINE_BUFFER_SIZE];

    while (true)
    {
        await_line(at_buffer, pdMS_TO_TICKS(1000));
        if(strstr(at_buffer, "APP RDY")){
            break;
        }
        else if(strstr(at_buffer, "NORMAL POWER DOWN")){
            hard_reset_cellular();
            continue;
        }
    }

    ESP_LOGI(TAG, "BG started");

    at_command_echo_set(false);
    int at_result = at_command_at();

    while(at_result < 0){
        ESP_LOGE(TAG, "bad response from modem: %d, retrying ", at_result);
        vTaskDelay(pdMS_TO_TICKS(1000));
        at_result = at_command_at();
    }

    ESP_LOGI(TAG, "[BG] Go for prodtest");
    return 0;
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
