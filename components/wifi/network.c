/* Common functions for protocol examples, to establish Wi-Fi or Ethernet connection.

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
 */

#include <string.h>
#include "protocol_examples_common.h"
#include "sdkconfig.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#if CONFIG_EXAMPLE_CONNECT_ETHERNET
#include "esp_eth.h"
#endif
#include "esp_log.h"
#include "esp_netif.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "network.h"
#include "nvs_flash.h"
#include "../../main/storage.h"
#include "../../main/connectivity.h"
#include "../zaptec_cloud/include/zaptec_cloud_listener.h"


char WifiSSID[32]= {0};
char WifiPSK[64] = {0};

static char previousWifiSSID[32] = {0};
static char previousWifiPSK[64] = {0};

char ip4Address[16] = {0};
char ip6Address;
static bool wifiScan = false;
static bool wifiIsValid = false;
//static bool connecting = false;

#define GOT_IPV4_BIT BIT(0)
#define GOT_IPV6_BIT BIT(1)
#undef CONFIG_EXAMPLE_CONNECT_IPV6
#ifdef CONFIG_EXAMPLE_CONNECT_IPV6
#define CONNECTED_BITS (GOT_IPV4_BIT | GOT_IPV6_BIT)
#else
#define CONNECTED_BITS (GOT_IPV4_BIT)
#endif

static EventGroupHandle_t s_connect_event_group;
static esp_ip4_addr_t s_ip_addr;
static const char *s_connection_name;
static esp_netif_t *netif = NULL;

#ifdef CONFIG_EXAMPLE_CONNECT_IPV6
static esp_ip6_addr_t s_ipv6_addr;
#endif

static const char *TAG = "NETWORK ";

bool isProductionSetup = false;
bool isConnected = false;

bool network_WifiIsConnected()
{
	return isConnected;
}

/* set up connection, Wi-Fi or Ethernet */
static void start(void);

/* tear down connection, release resources */
static void stop(void);

static void on_got_ip(void *arg, esp_event_base_t event_base,
                      int32_t event_id, void *event_data)
{
	isConnected = true;
    ESP_LOGI(TAG, "Got IP event!");
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    memcpy(&s_ip_addr, &event->ip_info.ip, sizeof(s_ip_addr));
    xEventGroupSetBits(s_connect_event_group, GOT_IPV4_BIT);

    //ip4Address = ip4addr_ntoa(&event->ip_info.ip.addr);
    esp_netif_ip_info_t ip_info;
        esp_netif_get_ip_info(IP_EVENT_STA_GOT_IP,&ip_info);


	ESP_LOGI(TAG, "IP4 Address:" IPSTR, IP2STR(&event->ip_info.ip));

	sprintf(ip4Address, IPSTR, IP2STR(&event->ip_info.ip));
	ESP_LOGI(TAG, "IP4 Address string: %s, %d",ip4Address, strlen(ip4Address));

}

#ifdef CONFIG_EXAMPLE_CONNECT_IPV6

static void on_got_ipv6(void *arg, esp_event_base_t event_base,
                        int32_t event_id, void *event_data)
{
    ip_event_got_ip6_t *event = (ip_event_got_ip6_t *)event_data;
    if (event->esp_netif != s_example_esp_netif) {
        ESP_LOGD(TAG, "Got IPv6 from another netif: ignored");
        return;
    }
    ESP_LOGI(TAG, "Got IPv6 event!");
    memcpy(&s_ipv6_addr, &event->ip6_info.ip, sizeof(s_ipv6_addr));
    xEventGroupSetBits(s_connect_event_group, GOT_IPV6_BIT);
}

#endif // CONFIG_EXAMPLE_CONNECT_IPV6

esp_err_t network_connect_wifi(bool productionSetup)
{
	isProductionSetup = productionSetup;

	/*ESP_ERROR_CHECK(esp_netif_init());
	esp_event_loop_create_default();

    //if (s_connect_event_group != NULL) {
    //    return ESP_ERR_INVALID_STATE;
    //}


	s_connect_event_group = xEventGroupCreate();
*/
  	start();

	ESP_LOGI(TAG, "Waiting for IP");
    xEventGroupWaitBits(s_connect_event_group, CONNECTED_BITS, true, true, portMAX_DELAY);

    /*if(isConnected == false)
    {
    	
    	vEventGroupDelete(s_connect_event_group);
    	s_connect_event_group = NULL;
    }
    else
    {*/
		ESP_LOGI(TAG, "Connected to %s", WifiSSID);
		ESP_LOGI(TAG, "IPv4 address: " IPSTR, IP2STR(&s_ip_addr));
	#ifdef CONFIG_EXAMPLE_CONNECT_IPV6
		ESP_LOGI(TAG, "IPv6 address: " IPV6STR, IPV62STR(s_ipv6_addr));
	#endif
    //}

    return ESP_OK;
}

esp_err_t network_disconnect_wifi(void)
{
    if (s_connect_event_group == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    vEventGroupDelete(s_connect_event_group);
    s_connect_event_group = NULL;
    stop();
    ESP_LOGI(TAG, "Disconnected from %s", s_connection_name);
    s_connection_name = NULL;

    return ESP_OK;
}

#ifdef CONFIG_EXAMPLE_CONNECT_WIFI

static void on_wifi_disconnect(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    ESP_LOGI(TAG, "Wi-Fi disconnected, trying to reconnect...");
    isConnected = false;

    //esp_wifi_disconnect();
    //esp_wifi_stop();
    //esp_wifi_start();
    esp_wifi_connect();
    xEventGroupSetBits(s_connect_event_group, CONNECTED_BITS);
}

#ifdef CONFIG_EXAMPLE_CONNECT_IPV6

static void on_wifi_connect(void *esp_netif, esp_event_base_t event_base,
                            int32_t event_id, void *event_data)
{
    esp_netif_create_ip6_linklocal(esp_netif);
}

#endif // CONFIG_EXAMPLE_CONNECT_IPV6

bool wifiStarted = false;

bool network_IsWifiStarted()
{
	return wifiStarted;
}

bool wifiIsInitialized = false;
void initWifi()
{
	if(wifiIsInitialized == false)
	{

		ESP_ERROR_CHECK(esp_netif_init());
		esp_event_loop_create_default();

		/*if (s_connect_event_group != NULL) {
			return ESP_ERR_INVALID_STATE;
		}*/


		s_connect_event_group = xEventGroupCreate();

		wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
		ESP_ERROR_CHECK(esp_wifi_init(&cfg));

		if(netif == NULL)
		{
			esp_netif_config_t netif_config = ESP_NETIF_DEFAULT_WIFI_STA();

			netif = esp_netif_new(&netif_config);

			esp_netif_attach_wifi_station(netif);
			esp_wifi_set_default_wifi_sta_handlers();
		}


		wifiIsInitialized = true;
	}
}

static void start(void)
{
	initWifi();

	/*wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));

	if(netif == NULL)
	{
		esp_netif_config_t netif_config = ESP_NETIF_DEFAULT_WIFI_STA();

    	netif = esp_netif_new(&netif_config);
	}
    	esp_netif_attach_wifi_station(netif);
    	esp_wifi_set_default_wifi_sta_handlers();


*/


    esp_err_t fault = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &on_wifi_disconnect, NULL);
    ESP_LOGI(TAG, "Event handler fault %d", fault);

    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_got_ip, NULL));
#ifdef CONFIG_EXAMPLE_CONNECT_IPV6
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, &on_wifi_connect, netif));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_GOT_IP6, &on_got_ipv6, NULL));
#endif

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = {0x0},
            .password = {0x0},
        },
    };

    if(isProductionSetup == true)
    {
    	strcpy(WifiSSID, "ZaptecHQ");
    	strcpy(WifiPSK, "LuckyJack#003");

    	memset(wifi_config.sta.ssid, 0, 32);
		memcpy(wifi_config.sta.ssid, WifiSSID, strlen(WifiSSID));

		memset(wifi_config.sta.password, 0, 64);
		memcpy(wifi_config.sta.password, WifiPSK, strlen(WifiPSK));
    }
    else
    {
		network_CheckWifiParameters();

		memset(wifi_config.sta.ssid, 0, 32);
		memcpy(wifi_config.sta.ssid, WifiSSID, strlen(WifiSSID));

		memset(wifi_config.sta.password, 0, 64);
		memcpy(wifi_config.sta.password, WifiPSK, strlen(WifiPSK));
    }

    ESP_LOGI(TAG, "Connecting to %s...", wifi_config.sta.ssid);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));

/*    while(wifiScan == true)
    {
    	ESP_LOGE(TAG,"wifiScan == true");
    	vTaskDelay(pdMS_TO_TICKS(1000));
    }
    */
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_connect());
    s_connection_name = "Wifi";
    wifiStarted = true;
}



static void startScan(void)
{
	initWifi();
    /*wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    if(netif == NULL)
    {
		esp_netif_config_t netif_config = ESP_NETIF_DEFAULT_WIFI_STA();
		netif = esp_netif_new(&netif_config);
    }
		esp_netif_attach_wifi_station(netif);
		esp_wifi_set_default_wifi_sta_handlers();
*/

  //  esp_err_t fault = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &on_wifi_disconnect, NULL);
    //ESP_LOGI(TAG, "Event handler fault %d", fault);

    //ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_got_ip, NULL));
#ifdef CONFIG_EXAMPLE_CONNECT_IPV6
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, &on_wifi_connect, netif));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_GOT_IP6, &on_got_ipv6, NULL));
#endif

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = {0x0},
            .password = {0x0},
        },
    };

    /*if(isProductionSetup == true)
    {
    	strcpy(WifiSSID, "ZaptecHQ");
    	strcpy(WifiPSK, "LuckyJack#003");

    	memset(wifi_config.sta.ssid, 0, 32);
		memcpy(wifi_config.sta.ssid, WifiSSID, strlen(WifiSSID));

		memset(wifi_config.sta.password, 0, 64);
		memcpy(wifi_config.sta.password, WifiPSK, strlen(WifiPSK));
    }
    else
    {
		network_CheckWifiParameters();

		memset(wifi_config.sta.ssid, 0, 32);
		memcpy(wifi_config.sta.ssid, WifiSSID, strlen(WifiSSID));

		memset(wifi_config.sta.password, 0, 64);
		memcpy(wifi_config.sta.password, WifiPSK, strlen(WifiPSK));
    }*/

    ESP_LOGI(TAG, "Connecting to %s...", wifi_config.sta.ssid);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));

/*    while(wifiScan == true)
    {
    	ESP_LOGE(TAG,"wifiScan == true");
    	vTaskDelay(pdMS_TO_TICKS(1000));
    }
    */
    ESP_ERROR_CHECK(esp_wifi_start());
    //ESP_ERROR_CHECK(esp_wifi_connect());
    s_connection_name = "Wifi";
    //wifiStarted = true;
}




static void stop(void)
{
    ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &on_wifi_disconnect));
    ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_got_ip));
#ifdef CONFIG_EXAMPLE_CONNECT_IPV6
    ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_GOT_IP6, &on_got_ipv6));
    ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, &on_wifi_connect));
#endif
    esp_err_t err = esp_wifi_stop();
    if (err == ESP_ERR_WIFI_NOT_INIT) {
        return;
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(esp_wifi_deinit());
    ESP_ERROR_CHECK(esp_wifi_clear_default_wifi_driver_and_handlers(netif));
    esp_netif_destroy(netif);
    netif = NULL;

    wifiStarted = false;
    wifiIsInitialized = false;
    isConnected = false;
}
#endif // CONFIG_EXAMPLE_CONNECT_WIFI

#ifdef CONFIG_EXAMPLE_CONNECT_ETHERNET

#ifdef CONFIG_EXAMPLE_CONNECT_IPV6

/** Event handler for Ethernet events */
static void on_eth_event(void *esp_netif, esp_event_base_t event_base,
                         int32_t event_id, void *event_data)
{
    switch (event_id) {
    case ETHERNET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "Ethernet Link Up");
        esp_netif_create_ip6_linklocal(esp_netif);
        break;
    default:
        break;
    }
}

#endif // CONFIG_EXAMPLE_CONNECT_IPV6

static esp_eth_handle_t s_eth_handle = NULL;
static esp_eth_mac_t *s_mac = NULL;
static esp_eth_phy_t *s_phy = NULL;
static void *s_eth_glue = NULL;

static void start(void)
{
    esp_netif_config_t netif_config = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *netif = esp_netif_new(&netif_config);
    assert(netif);
    s_example_esp_netif = netif;
    // Set default handlers to process TCP/IP stuffs
    ESP_ERROR_CHECK(esp_eth_set_default_handlers(netif));
    // Register user defined event handers
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &on_got_ip, NULL));
#ifdef CONFIG_EXAMPLE_CONNECT_IPV6
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ETHERNET_EVENT_CONNECTED, &on_eth_event, netif));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_GOT_IP6, &on_got_ipv6, NULL));
#endif
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.phy_addr = CONFIG_EXAMPLE_ETH_PHY_ADDR;
    phy_config.reset_gpio_num = CONFIG_EXAMPLE_ETH_PHY_RST_GPIO;
#if CONFIG_EXAMPLE_USE_INTERNAL_ETHERNET
    mac_config.smi_mdc_gpio_num = CONFIG_EXAMPLE_ETH_MDC_GPIO;
    mac_config.smi_mdio_gpio_num = CONFIG_EXAMPLE_ETH_MDIO_GPIO;
    s_mac = esp_eth_mac_new_esp32(&mac_config);
#if CONFIG_EXAMPLE_ETH_PHY_IP101
    s_phy = esp_eth_phy_new_ip101(&phy_config);
#elif CONFIG_EXAMPLE_ETH_PHY_RTL8201
    s_phy = esp_eth_phy_new_rtl8201(&phy_config);
#elif CONFIG_EXAMPLE_ETH_PHY_LAN8720
    s_phy = esp_eth_phy_new_lan8720(&phy_config);
#elif CONFIG_EXAMPLE_ETH_PHY_DP83848
    s_phy = esp_eth_phy_new_dp83848(&phy_config);
#endif
#elif CONFIG_EXAMPLE_USE_DM9051
    gpio_install_isr_service(0);
    spi_device_handle_t spi_handle = NULL;
    spi_bus_config_t buscfg = {
        .miso_io_num = CONFIG_EXAMPLE_DM9051_MISO_GPIO,
        .mosi_io_num = CONFIG_EXAMPLE_DM9051_MOSI_GPIO,
        .sclk_io_num = CONFIG_EXAMPLE_DM9051_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(CONFIG_EXAMPLE_DM9051_SPI_HOST, &buscfg, 1));
    spi_device_interface_config_t devcfg = {
        .command_bits = 1,
        .address_bits = 7,
        .mode = 0,
        .clock_speed_hz = CONFIG_EXAMPLE_DM9051_SPI_CLOCK_MHZ * 1000 * 1000,
        .spics_io_num = CONFIG_EXAMPLE_DM9051_CS_GPIO,
        .queue_size = 20
    };
    ESP_ERROR_CHECK(spi_bus_add_device(CONFIG_EXAMPLE_DM9051_SPI_HOST, &devcfg, &spi_handle));
    /* dm9051 ethernet driver is based on spi driver */
    eth_dm9051_config_t dm9051_config = ETH_DM9051_DEFAULT_CONFIG(spi_handle);
    dm9051_config.int_gpio_num = CONFIG_EXAMPLE_DM9051_INT_GPIO;
    s_mac = esp_eth_mac_new_dm9051(&dm9051_config, &mac_config);
    s_phy = esp_eth_phy_new_dm9051(&phy_config);
#elif CONFIG_EXAMPLE_USE_OPENETH
    phy_config.autonego_timeout_ms = 100;
    s_mac = esp_eth_mac_new_openeth(&mac_config);
    s_phy = esp_eth_phy_new_dp83848(&phy_config);
#endif

    // Install Ethernet driver
    esp_eth_config_t config = ETH_DEFAULT_CONFIG(s_mac, s_phy);
    ESP_ERROR_CHECK(esp_eth_driver_install(&config, &s_eth_handle));
    // combine driver with netif
    s_eth_glue = esp_eth_new_netif_glue(s_eth_handle);
    esp_netif_attach(netif, s_eth_glue);
    esp_eth_start(s_eth_handle);
    s_connection_name = "Ethernet";
}

static void stop(void)
{
    ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_ETH_GOT_IP, &on_got_ip));
#ifdef CONFIG_EXAMPLE_CONNECT_IPV6
    ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_GOT_IP6, &on_got_ipv6));
    ESP_ERROR_CHECK(esp_event_handler_unregister(ETH_EVENT, ETHERNET_EVENT_CONNECTED, &on_eth_event));
#endif
    ESP_ERROR_CHECK(esp_eth_stop(s_eth_handle));
    ESP_ERROR_CHECK(esp_eth_del_netif_glue(s_eth_glue));
    ESP_ERROR_CHECK(esp_eth_clear_default_handlers(s_example_esp_netif));
    ESP_ERROR_CHECK(esp_eth_driver_uninstall(s_eth_handle));
    ESP_ERROR_CHECK(s_phy->del(s_phy));
    ESP_ERROR_CHECK(s_mac->del(s_mac));

    esp_netif_destroy(s_example_esp_netif);
    s_example_esp_netif = NULL;
}

#endif // CONFIG_EXAMPLE_CONNECT_ETHERNET

esp_netif_t *get_example_netif(void)
{
    return netif;
}

void SetupWifi()
{
	start();
}


char * network_GetIP4Address()
{
	return ip4Address;
}

float network_WifiSignalStrength()
{
	wifi_ap_record_t wifidata;
	float wifiRSSI = 0.0;
	if (esp_wifi_sta_get_ap_info(&wifidata)==0)
		wifiRSSI= (float)wifidata.rssi;

	return wifiRSSI;
}


char * network_getWifiSSID()
{
	return WifiSSID;
}


bool network_CheckWifiParameters()
{
	network_clearWifi();
	esp_err_t err = storage_ReadWifiParameters(WifiSSID, WifiPSK);
	int ssidLength = strlen(WifiSSID);

	if((32 >= ssidLength) && (ssidLength >= 1))
	{
		if(err == ESP_OK)
			wifiIsValid = true;
		else
			wifiIsValid = false;
	}
	else
	{
		wifiIsValid = false;
	}

	return wifiIsValid;
}

bool network_wifiIsValid()
{
	return wifiIsValid;
}


void network_updateWifi()
{

	if(connectivity_GetPreviousInterface() != eCONNECTION_WIFI)
		return;
	/*if(wifiStarted)
	{
		network_disconnect_wifi();
		network_connect_wifi(false);
	}
	return;*/


	if(network_wifiIsValid())
	{
		//Only update if both
		//if((strcmp(previousWifiSSID, WifiSSID) != 0) || (strcmp(previousWifiPSK, WifiPSK) != 0) )
		//{

			ESP_ERROR_CHECK( esp_wifi_stop() );

			wifi_config_t wifi_config = {
					.sta = {
						.ssid = {0x0},
						.password = {0x0},

					},
				};

			memset(wifi_config.sta.ssid, 0, 32);
			memcpy(wifi_config.sta.ssid, WifiSSID, strlen(WifiSSID));

			memset(wifi_config.sta.password, 0, 64);
			memcpy(wifi_config.sta.password, WifiPSK, strlen(WifiPSK));

			ESP_LOGI(TAG, "Setting WiFi configuration SSID %s PSK %s", (char*)wifi_config.sta.ssid, (char*)wifi_config.sta.password);
			ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
			ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
			ESP_ERROR_CHECK( esp_wifi_start() );
			ESP_ERROR_CHECK(esp_wifi_connect());

			//Hold the new values
			memcpy(previousWifiSSID, WifiSSID, 32);
			memcpy(previousWifiPSK, WifiPSK, 64);
		//}
	}
}



void network_stopWifi()
{
	esp_wifi_stop();
}

void network_clearWifi()
{
	memset(WifiSSID, 0, 32);
	memset(WifiPSK, 0, 64);
}


void network_startWifiScan()
{


	/*while(wifiStarted == false)
	{
		ESP_LOGE(TAG,"wifiStarted == false");
		vTaskDelay(pdMS_TO_TICKS(1000));
	}*/

	wifiScan = true;

	if(!wifiStarted)
		startScan();
	else
	{
		//stop_cloud_listener_task();
		esp_wifi_disconnect();
		esp_wifi_stop();
	}

	/*if(network_IsWifiStarted() == false)
		start();
		network_connect_wifi(false);
	else*/
		//esp_wifi_disconnect();

	//if(firstTime)
	//{
		//initialise_wifi(0);
		/*tcpip_adapter_init();
		wifi_event_group = xEventGroupCreate();
		ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL) );
		wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
		ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
		ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM));//WIFI_STORAGE_FLASH ) );
*/
		//firstTime = false;
	//}

    wifi_scan_config_t scanConf = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
		.scan_time.active.max = 300,
		.scan_time.active.min = 0,
    };


    /*while(connecting == true)
    {
    	ESP_LOGE(TAG, "Waiting for connecting release");
    	vTaskDelay(pdMS_TO_TICKS(1000));
    }*/

    //ESP_ERROR_CHECK( esp_wifi_start() );


    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_ERROR_CHECK( esp_wifi_start() );
    ESP_ERROR_CHECK(esp_wifi_scan_start(&scanConf, true));
    ESP_ERROR_CHECK(esp_wifi_scan_stop());

    //if(network_wifiIsValid() == false)
    //	ESP_ERROR_CHECK( esp_wifi_stop() );

    /*wifi_scan_config_t config = {
			.scan_method = WIFI_ALL_CHANNEL_SCAN,
            .sort_method = WIFI_CONNECT_AP_BY_SIGNAL,
	};
	bool block = false;
	esp_wifi_scan_start(config, block);*/

    //esp_wifi_stop();

    wifiScan = false;

    //network_disconnect_wifi();
}


void network_WifiScanEnd()
{
	//wifiIsInitialized = false;
	esp_wifi_stop();
    //ESP_ERROR_CHECK(esp_wifi_deinit());
	//ESP_ERROR_CHECK(esp_wifi_clear_default_wifi_driver_and_handlers(netif));
	//esp_netif_destroy(netif);
}

//static bool wait_for_ip(bool prod)
//{
//    bool successfulConnection = 0;
//
//	uint32_t bits = IPV4_GOTIP_BIT;// | IPV6_GOTIP_BIT ;
//
//	TickType_t xTicksToWait;
//
//	/*if(prod == true)
//		xTicksToWait = 20000 / portTICK_PERIOD_MS;
//	else*/
//	xTicksToWait = 5000 / portTICK_PERIOD_MS;
//
//    ESP_LOGI(TAG, "Waiting for AP connection...");
//    EventBits_t uxBits = xEventGroupWaitBits(wifi_event_group, bits, false, true, xTicksToWait);
//
//    if(uxBits == 0)
//    {
//    	esp_wifi_stop();
//    	connecting = false;
//    	successfulConnection = 0;
//		ESP_LOGI(TAG, "Deinit wifi, bits: %d, uxBits: %d", bits, uxBits);
//
//		//Delay before new round - need to charge capasitor
//
//		if(prod == false)
//		{
//				vTaskDelay(pdMS_TO_TICKS(2000));
//				ESP_LOGI(TAG, "Waiting 2 sec");
//		}
//    }
//    else
//    {
//    	ESP_LOGI(TAG, "Connected to AP, bits: %d, uxBits: %d", bits, uxBits);
//    	successfulConnection = 1;
//    }
//
//    return successfulConnection;
//}


bool network_renewConnection()
{
	if(wifiScan == true)
		return false;

	network_updateWifi();
	return true;//wait_for_ip(false);
}

void network_SendRawTx()
{
	uint8_t beacon_raw[] = {
		0x80, 0x00,							// 0-1: Frame Control
		0x00, 0x00,							// 2-3: Duration
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff,				// 4-9: Destination address (broadcast)
		0xba, 0xde, 0xaf, 0xfe, 0x00, 0x06,				// 10-15: Source address
		0xba, 0xde, 0xaf, 0xfe, 0x00, 0x06,				// 16-21: BSSID
		0x00, 0x00,							// 22-23: Sequence / fragment number
		0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,			// 24-31: Timestamp (GETS OVERWRITTEN TO 0 BY HARDWARE)
		0x64, 0x00,							// 32-33: Beacon interval
		0x31, 0x04,							// 34-35: Capability info
		0x00, 0x00, /* FILL CONTENT HERE */				// 36-38: SSID parameter set, 0x00:length:content
		0x01, 0x08, 0x82, 0x84,	0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24,	// 39-48: Supported rates
		0x03, 0x01, 0x01,						// 49-51: DS Parameter set, current channel 1 (= 0x01),
		0x05, 0x04, 0x01, 0x02, 0x00, 0x00,				// 52-57: Traffic Indication Map
		0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x00,
	};

	volatile uint8_t messageLength = sizeof(beacon_raw);
	esp_wifi_80211_tx(ESP_IF_WIFI_STA, beacon_raw, messageLength, false);
	ESP_LOGW(TAG, "Sending Wifi TX");
}
