#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>
#include "esp_log.h"
#include "cJSON.h"
//#include "wpa2/utils/base64.h"
#include "base64.h"
#include "esp_sleep.h"


#include "diagnostics_port.h"
//#include "m_bus.h"
#include "adc_control.h"
#include "driver/uart.h"
#include "protocol_task.h"

//#include "mdns.h"
//#include <sys/socket.h>
//#include <netdb.h>
//
//#define EXAMPLE_MDNS_INSTANCE ("InstanceName")//CONFIG_MDNS_INSTANCE
//static const char c_config_hostname[] = "espressif32";//CONFIG_MDNS_HOSTNAME;
///* The event group allows multiple bits for each event,
//   but we only care about one event - are we connected
//   to the AP with an IP? */
//const int IP4_CONNECTED_BIT = BIT0;
//const int IP6_CONNECTED_BIT = BIT1;


//Radio tester
#define EXAMPLE_WIFI_SSID "CMW-AP"
#define EXAMPLE_WIFI_PASS ""//tk51mo79"

//Radio tester
//#define EXAMPLE_WIFI_SSID "APPLICA-GJEST"
//#define EXAMPLE_WIFI_PASS "Deter1findagidag!"

//#define EXAMPLE_WIFI_SSID "BVb"
//#define EXAMPLE_WIFI_PASS ""//tk51mo79"

//#define EXAMPLE_WIFI_SSID CONFIG_WIFI_SSID
//#define EXAMPLE_WIFI_PASS CONFIG_WIFI_PASSWORD

#define PORT 53388


/*typedef struct CJSON {
	int identifier;
	float wifiSSID;
	float HANenergy;
	char *string;
};*/


/* FreeRTOS event group to signal when we are connected & ready to make a request */
static EventGroupHandle_t wifi_event_group;

const int IPV4_GOTIP_BIT = BIT0;
const int IPV6_GOTIP_BIT = BIT1;

static const char *TAG = "NETWORK:";
int sock;

//extern uint8_t *obisRawData;

static esp_err_t event_handler(void *ctx, system_event_t *event)
{
	ESP_LOGI(TAG, "Event handler");

    switch (event->event_id) {

    case SYSTEM_EVENT_AP_START:
		ESP_LOGI(TAG, "SoftAP started");
		break;
	case SYSTEM_EVENT_AP_STOP:
		ESP_LOGI(TAG, "SoftAP stopped");
		break;

    case SYSTEM_EVENT_STA_START:
        esp_wifi_connect();
        ESP_LOGI(TAG, "SYSTEM_EVENT_STA_START");
        break;
    case SYSTEM_EVENT_STA_CONNECTED:
        /* enable ipv6 */
        tcpip_adapter_create_ip6_linklocal(TCPIP_ADAPTER_IF_STA);
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        xEventGroupSetBits(wifi_event_group, IPV4_GOTIP_BIT);
        ESP_LOGI(TAG, "SYSTEM_EVENT_STA_GOT_IP");
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        /* This is a workaround as ESP32 WiFi libs don't currently auto-reassociate. */
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, IPV4_GOTIP_BIT);
        xEventGroupClearBits(wifi_event_group, IPV6_GOTIP_BIT);
        break;
    case SYSTEM_EVENT_AP_STA_GOT_IP6:
        xEventGroupSetBits(wifi_event_group, IPV6_GOTIP_BIT);
        ESP_LOGI(TAG, "SYSTEM_EVENT_STA_GOT_IP6");

        char *ip6 = ip6addr_ntoa(&event->event_info.got_ip6.ip6_info.ip);
        ESP_LOGI(TAG, "IPv6: %s", ip6);
        break;
    default:
        break;
    }
    return ESP_OK;
}

static void initialise_wifi(void)
{
    tcpip_adapter_init();
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL) );
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_WIFI_SSID,
            .password = EXAMPLE_WIFI_PASS,
        },
    };
    ESP_LOGI(TAG, "Setting WiFi configuration SSID %s...", wifi_config.sta.ssid);
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    ESP_ERROR_CHECK( esp_wifi_start() );

    //esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    esp_wifi_set_ps(WIFI_PS_MAX_MODEM);
}

static void wait_for_ip()
{
    uint32_t bits = IPV4_GOTIP_BIT | IPV6_GOTIP_BIT ;

    ESP_LOGI(TAG, "Waiting for AP connection...");
    xEventGroupWaitBits(wifi_event_group, bits, false, true, portMAX_DELAY);
    ESP_LOGI(TAG, "Connected to AP");
}




#define ECHO_TEST_TXD  (GPIO_NUM_17)
#define ECHO_TEST_RXD  (GPIO_NUM_16)
#define ECHO_TEST_RTS  (UART_PIN_NO_CHANGE)
#define ECHO_TEST_CTS  (UART_PIN_NO_CHANGE)

#define BUF_SIZE (1024)

static void tcp_server_task(void *pvParameters)
{
    char rx_buffer[50];
    //char tx_buffer[54];
    char addr_str[128];
    int addr_family;
    int ip_protocol;
    unsigned int connectionTimout = 17000;
    //int value;
    int firstTime = 0;
    int err;
    int listen_sock;
    int mbusTimeout = 150;
    unsigned int status = 0;

    while (1) {
#define  CONFIG_EXAMPLE_IPV4
#ifdef CONFIG_EXAMPLE_IPV4
        struct sockaddr_in destAddr;
        destAddr.sin_addr.s_addr = htonl(INADDR_ANY);
        destAddr.sin_family = AF_INET;
        destAddr.sin_port = htons(PORT);
        addr_family = AF_INET;
        ip_protocol = IPPROTO_IP;
        inet_ntoa_r(destAddr.sin_addr, addr_str, sizeof(addr_str) - 1);
#else // IPV6
        struct sockaddr_in6 destAddr;
        bzero(&destAddr.sin6_addr.un, sizeof(destAddr.sin6_addr.un));
        destAddr.sin6_family = AF_INET6;
        destAddr.sin6_port = htons(PORT);
        addr_family = AF_INET6;
        ip_protocol = IPPROTO_IPV6;
        inet6_ntoa_r(destAddr.sin6_addr, addr_str, sizeof(addr_str) - 1);
#endif

        if(firstTime == 0)
        {
        	listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
			if (listen_sock < 0) {
				ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
				break;
			}
			ESP_LOGI(TAG, "Socket created");

			//value = 1;
			//int err = setsockopt(listen_sock,SOL_SOCKET,SO_REUSEADDR,&value,sizeof(int));
			//if (err != 0) {
			//	ESP_LOGE(TAG, "Socket reuseaddr: errno %d", errno);
			//	break;
			//}
			//err = setsockopt(listen_sock,SOL_SOCKET,SO_LINGER,&value,sizeof(int));
			//if (err != 0) {
			//	ESP_LOGE(TAG, "Socket linger: errno %d", errno);
			//	break;
			//}



			int err = bind(listen_sock, (struct sockaddr *)&destAddr, sizeof(destAddr));
			if (err != 0) {
				ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
				//shutdown(listen_sock, 0);
				//close(listen_sock);
				break;
			}
			ESP_LOGI(TAG, "Socket binded");
        }
        //firstTime = 1;


        err = listen(listen_sock, 3);
        if (err != 0) {
            ESP_LOGE(TAG, "Error occured during listen: errno %d", errno);
            break;
        }
        ESP_LOGI(TAG, "Socket listening");

        struct sockaddr_in6 sourceAddr; // Large enough for both IPv4 or IPv6
        uint addrLen = sizeof(sourceAddr);
        sock = accept(listen_sock, (struct sockaddr *)&sourceAddr, &addrLen);

        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
            break;
        }
        ESP_LOGI(TAG, "Socket accepted");


		struct timeval tv;
		tv.tv_sec = 1;
		tv.tv_usec = 0;
		int err_ = setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*) &tv, sizeof(struct timeval));
		err_ =err_ + 1;
//        int n;
//        struct arpreq arpreq_;
//        bzero(&arpreq_, sizeof(struct arpreq));
//        if( ( n = ioctl(sock, SIOCGARP, &arpreq_) ) < 0 ){
//        perror("ioctl");
//        }
//
//        unsigned char *ptr = &arpreq_.arp_ha.sa_data[0];
//        printf("MAC: %x:%x:%x:%x:%x:%x\n", *ptr, *(ptr+1), *(ptr+2), *(ptr+3),
//        *(ptr+4), *(ptr+5));


		//if(firstTime == 0)
		//{

			/* Configure parameters of an UART driver,
			 * communication pins and install the driver */
//			uart_config_t uart_config = {
//				.baud_rate = 2400,
//				.data_bits = UART_DATA_8_BITS,
//				.parity    = UART_PARITY_DISABLE,
//				.stop_bits = UART_STOP_BITS_1,
//				.flow_ctrl = UART_HW_FLOWCTRL_DISABLE
//			};
//			uart_param_config(UART_NUM_2, &uart_config);
//			uart_set_pin(UART_NUM_2, ECHO_TEST_TXD, ECHO_TEST_RXD, ECHO_TEST_RTS, ECHO_TEST_CTS);
//			uart_driver_install(UART_NUM_2, BUF_SIZE * 2, 0, 0, NULL, 0);
		//}
		firstTime = 1;

			// Configure a temporary buffer for the incoming data
		//uint8_t * readData = (uint8_t *) malloc(150);
		uint8_t * holdData = (uint8_t *) malloc(1024);

	    int receivedLength = 0;






        cJSON *jsonObject = NULL;
        unsigned char * string64;// = (unsigned char *) malloc(500);
        char * jsonString;// = (char *) malloc(600);
        wifi_ap_record_t wifidata;

        int len = 0;
        unsigned int byteCount = 0;
        unsigned int bufferSize = 0;
        unsigned int value = 0;
        while (1) {
        	vTaskDelay(1);
        	bufferSize = 0;

        	//uart_get_buffered_data_len(UART_NUM_2, &bufferSize);
//        	if(bufferSize >= 4)
//        	{
//        		len = uart_read_bytes(UART_NUM_2, readData, 4, 20 / portTICK_RATE_MS);
//        		if((readData[3] == 0xee) && (readData[2] == 0xaa))
//        		{
//        			value = (readData[1] << 8) + readData[0];
//        		}
//        		else
//        		{
//        			ESP_LOGE(TAG, "Invalid AMS bytes, flushing buffer");
//        			uart_flush(UART_NUM_2);
//        			continue;
//        		}
//
//        		if(len == 0)
//        			continue;
//        	}
//        	else
//        	{
//        		continue;
//        	}

//			if(len <= 1)
//				ESP_LOGE(TAG, "Error: Only 1 byte read");

			byteCount += 1;

			if(byteCount % 100 == 0)
			{
				//ESP_LOGE(TAG, "ByteCount: %d", byteCount);
				size_t heapSize = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
				ESP_LOGI(TAG, "Heap size : %d  VALUE: %d", heapSize, value);
				//ESP_LOGI(TAG, "Value : %d", value);
			}

			//value = (readData[1] << 8) + readData[0];

			//value++;

			float wifiRSSI = 0.0;
			if (esp_wifi_sta_get_ap_info(&wifidata)==0)
				wifiRSSI= (float)wifidata.rssi;

			int8_t power = 0;
			esp_wifi_get_max_tx_power(&power);

			jsonObject = cJSON_CreateObject();
			cJSON_AddNumberToObject(jsonObject, "MCUcnt", MCU_GetDebugCounter());
			cJSON_AddNumberToObject(jsonObject, "ESPcnt", byteCount);

			cJSON_AddNumberToObject(jsonObject, "WifiRSSI", wifiRSSI);

			cJSON_AddNumberToObject(jsonObject, "HwId", GetHardwareId());
			cJSON_AddNumberToObject(jsonObject, "PwrMeas", GetPowerMeas());

			cJSON_AddNumberToObject(jsonObject, "Warning", MCU_GetWarnings());

			cJSON_AddNumberToObject(jsonObject, "MCUrst", MCU_GetResetSource());
			cJSON_AddNumberToObject(jsonObject, "ESPrst", (unsigned int)esp_reset_reason());

			cJSON_AddNumberToObject(jsonObject, "TxP", power);

			jsonString = cJSON_Print(jsonObject);
			len = strlen(jsonString);
			//ESP_LOGE(TAG, "Sending jsonString length: %d", len);
			//ESP_LOGE(TAG, "Transmit no: %d", measurementNo);
			err = send(sock, jsonString, len, 0);

			//Clean up heap items
			free(jsonString);
			cJSON_Delete(jsonObject);

			if (err < 0) {
				ESP_LOGE(TAG, "Error occured during sending: errno %d", errno);

				//if(errno == 104)
				//{
				ESP_LOGE(TAG, "Shutting down socket to allow new connection");
				shutdown(sock, 2);//0);
				close(sock);
				sock = 0;
				//}
				break;
				//continue;
			}

        }

        /*if ((sock != -1)) {// || (connectionTimout == 0)) {
            ESP_LOGE(TAG, "Shutting down socket and restarting...");
            shutdown(sock, 2);//0);
            close(sock);
            sock = 0;
            //shutdown(listen_sock, 0);
            //close(listen_sock);
            ESP_LOGE(TAG, "Socket closed");
        }*/
    }
    vTaskDelete(NULL);
}


int network_getSocket()
{
	return sock;
}




void network_init()
{
	//initialise_wifi();
	//wait_for_ip();

	xTaskCreate(tcp_server_task, "tcp_server", 4096, NULL, 5, NULL);

}
