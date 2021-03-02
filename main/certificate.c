#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "certificate.h"

#include "mbedtls/ecdsa.h"
#include "mbedtls/pk.h"
#include <string.h>
#include "esp_http_client.h"
#include "cJSON.h"
#include "network.h"
#include "../cellular_modem/include/ppp_task.h"
#include "esp_tls.h"
#include "i2cDevices.h"
#include "fat.h"
#include "esp_crt_bundle.h"

#define u8 uint8_t

#include "../wpa_supplicant/src/crypto/sha256.h"

#define MAX_HTTP_RECV_BUFFER 512

static const char *TAG = "CERT    ";

static TaskHandle_t taskCertHandle = NULL;

extern const uint8_t zap_cert_pem_start[] asm("_binary_zaptec_ca_cer_start");
extern const uint8_t zap_cert_pem_end[] asm("_binary_zaptec_ca_cer_end");

extern const uint8_t bundle_crt_start[] asm("_binary_bundle_crt_start");
extern const uint8_t bundle_crt_end[] asm("_binary_bundle_crt_end");

static const unsigned char zaptecPublicKey[] = "-----BEGIN PUBLIC KEY-----\nMFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEvTEC5cEvbSNkBksOwRItuhBUf3my\n7Eo0EO9Z784bTQ01PkUZcT5JnkFkGRVTzvLlMqNYZvZIGQLfkJqffSFMZA==\n-----END PUBLIC KEY-----\0";

static esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    static char *output_buffer;  // Buffer to store response of http request from event handler
    static int output_len;       // Stores number of bytes read
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
            /*
             *  Check for chunked encoding is added as the URL for chunked encoding used in this example returns binary data.
             *  However, event handler can also be used in case chunked encoding is used.
             */
            if (!esp_http_client_is_chunked_response(evt->client)) {
                // If user_data buffer is configured, copy the response into the buffer
                if (evt->user_data) {
                    memcpy(evt->user_data + output_len, evt->data, evt->data_len);
                } else {
                    if (output_buffer == NULL) {
                        output_buffer = (char *) malloc(esp_http_client_get_content_length(evt->client));
                        output_len = 0;
                        if (output_buffer == NULL) {
                            ESP_LOGE(TAG, "Failed to allocate memory for output buffer");
                            return ESP_FAIL;
                        }
                    }
                    memcpy(output_buffer + output_len, evt->data, evt->data_len);
                }
                output_len += evt->data_len;
            }

            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            if (output_buffer != NULL) {
                // Response is accumulated in output_buffer. Uncomment the below line to print the accumulated response
                // ESP_LOG_BUFFER_HEX(TAG, output_buffer, output_len);
                free(output_buffer);
                output_buffer = NULL;
            }
            output_len = 0;
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            int mbedtls_err = 0;
            esp_err_t err = esp_tls_get_and_clear_last_error(evt->data, &mbedtls_err, NULL);
            if (err != 0) {
                if (output_buffer != NULL) {
                    free(output_buffer);
                    output_buffer = NULL;
                }
                output_len = 0;
                ESP_LOGI(TAG, "Last esp error code: 0x%x", err);
                ESP_LOGI(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
            }
            break;
    }
    return ESP_OK;
}

unsigned char certBuf[40000] = {0};
bool ParseCertificateBundle(char * certificateBundle)
{
	int receivedVersion = 0;
	char receivedSign[200] = {0};
	cJSON *body = cJSON_Parse(certificateBundle);
	if(body!=NULL){
		if(cJSON_HasObjectItem(body, "ver")){
			receivedVersion =  cJSON_GetObjectItem(body, "ver")->valueint;
			ESP_LOGW(TAG, "Received version: %d", receivedVersion);
		}
		if(cJSON_HasObjectItem(body, "data")){
			//strcpy(receivedSign, cJSON_GetObjectItem(body, "sign")->valuestring);
			ESP_LOGW(TAG, "Received cert len: %d", strlen(cJSON_GetObjectItem(body, "data")->valuestring));
		}

		if(cJSON_HasObjectItem(body, "sign")){
			strcpy(receivedSign, cJSON_GetObjectItem(body, "sign")->valuestring);
			ESP_LOGW(TAG, "Received signature: %s", receivedSign);
		}




		/*esp_err_t err = esp_tls_init_global_ca_store();
		if(err != ESP_OK)
			printf("Creating store failed: %i\n", err);
*/
		unsigned int ca_len = strlen(cJSON_GetObjectItem(body, "data")->valuestring);

		//memcpy(certBuf, cJSON_GetObjectItem(body, "data")->valuestring, ca_len);

		/*esp_tls_cfg_t cfg = {
		     .crt_bundle_attach = certBuf,
		};

		mbedtls_ssl_config conf;
		mbedtls_ssl_config_init(&conf);

		esp_crt_bundle_attach(&conf);*/

		//esp_err_t err = esp_tls_set_global_ca_store(certBuf, ca_len);
		esp_err_t err = esp_tls_set_global_ca_store(zap_cert_pem_start, zap_cert_pem_end - zap_cert_pem_start);
		if(err != ESP_OK)
			printf("Creating store failed: %i\n", err);



		cJSON_Delete(body);
	}
	return true;
}


//int certificateGetNew()
static void certificate_task()
{

	/*char certificate_location[50000] = {0};
	esp_http_client_config_t config = {
		.url = certificate_location,
		.cert_pem = (char *)server_cert_pem_start,
		// .use_global_ca_store = true,
		.event_handler = _http_event_handler,
		.timeout_ms = 20000,
		.buffer_size = 1536,
	};*/

	bool newCertificateRequired = false;

	char *certificate_location = calloc(50000,1);

	fat_ReadCertificate(certificate_location);

	bool isOk = ParseCertificateBundle(certificate_location);
	if(!isOk)
		newCertificateRequired = true;

	while(newCertificateRequired)
	{

		while((network_WifiIsConnected() == false) && (LteIsConnected() == false))
		{
			vTaskDelay(pdMS_TO_TICKS(1000));
		}


		size_t free_dram = heap_caps_get_free_size(MALLOC_CAP_8BIT);
		size_t low_dram = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
		ESP_LOGE(TAG, "MEM1: DRAM: %i Lo: %i", free_dram, low_dram);

		char * url = "https://devices.zaptec.com/bundle";

		ESP_LOGI(TAG, "Get cert from: %s", url);

		//char certificate_location[1536] = {0};
		esp_http_client_config_t config = {
			.url = url,
			.transport_type = HTTP_TRANSPORT_OVER_SSL,
			.event_handler = _http_event_handler,
			.user_data = certificate_location,
			.cert_pem = (char *)zap_cert_pem_start,
			.timeout_ms = 30000,
			.buffer_size = 1024,
		};

		esp_http_client_handle_t client = esp_http_client_init(&config);

		// POST
		int receivedVersion = 0;
		char receivedSign[200] = {0};

		char post_data [100] = {0};
		snprintf(post_data, 100,"{\"ver\":6, \"serial\": \"%s\"}", i2cReadDeviceInfoFromEEPROM().serialNumber);
		//char * post_data = "{\"ver\":7, \"serial\": \"}";

		esp_http_client_set_method(client, HTTP_METHOD_POST);
		esp_http_client_set_header(client, "Content-Type", "application/json");
		//esp_http_client_set_post_field(client, post_data, strlen(post_data));


		int postlen = strlen(post_data);
		volatile esp_err_t err;
		if ((err = esp_http_client_open(client, postlen)) != ESP_OK) {
			ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
			free(certificate_location);
			return;
		}

		err = esp_http_client_write(client, post_data, postlen);

		volatile int content_length =  esp_http_client_fetch_headers(client);
		volatile int total_read_len = 0;
		volatile int read_len = 0;

		read_len = esp_http_client_read(client, certificate_location, 50000);


		ESP_LOGW(TAG, "Len: %d, Body: %c%c%c ... %c%c%c  ", strlen(certificate_location), certificate_location[0], certificate_location[1], certificate_location[2], certificate_location[read_len-3], certificate_location[read_len-2], certificate_location[read_len-1]);

		cJSON *body = cJSON_Parse(certificate_location);
		if(body!=NULL){
			if(cJSON_HasObjectItem(body, "ver")){
				receivedVersion =  cJSON_GetObjectItem(body, "ver")->valueint;
				ESP_LOGW(TAG, "Received version: %d", receivedVersion);
			}
			if(cJSON_HasObjectItem(body, "data")){
				//strcpy(receivedSign, cJSON_GetObjectItem(body, "sign")->valuestring);
				ESP_LOGW(TAG, "Received cert len: %d", strlen(cJSON_GetObjectItem(body, "data")->valuestring));
			}

			if(cJSON_HasObjectItem(body, "sign")){
				strcpy(receivedSign, cJSON_GetObjectItem(body, "sign")->valuestring);
				ESP_LOGW(TAG, "Received signature: %s", receivedSign);
			}

			cJSON_Delete(body);

			//const char sig[] = "304402205882d3826fc16f9da91700ed715b5288736077dcc27a3968594aeb8765bf9c33022031e4127eb145d9eaf5177912ae2a7281820e9556d9ef144789372d4e6312a7c3";

			//memset(certificate_location,0, 50000);
			//memcpy(certificate_location, sig, sizeof(sig));

			fat_WriteCertificate(certificate_location);

			memset(certificate_location,0, sizeof(char));

			fat_ReadCertificate(certificate_location);

			ESP_LOGW(TAG, "Len: %d, Body: %c%c%c ... %c%c%c  ", strlen(certificate_location), certificate_location[0], certificate_location[1], certificate_location[2], certificate_location[read_len-3], certificate_location[read_len-2], certificate_location[read_len-1]);

			fat_static_unmount();

		}
		else
		{
			ESP_LOGW(TAG, "bad body");
		}

		esp_http_client_close(client);
		esp_http_client_cleanup(client);

		newCertificateRequired = false;
	}
		/*if (total_read_len < content_length && content_length <= MAX_HTTP_RECV_BUFFER) {
			read_len = esp_http_client_read(client, certificate_location, content_length);
			if (read_len <= 0) {
				ESP_LOGE(TAG, "Error read data");
			}
			certificate_location[read_len] = 0;
			ESP_LOGD(TAG, "read_len = %d", read_len);
		}
		ESP_LOGI(TAG, "HTTP Stream reader Status = %d, content_length = %d",
						esp_http_client_get_status_code(client),
						esp_http_client_get_content_length(client));
		esp_http_client_close(client);
		esp_http_client_cleanup(client);

		continue;

		//esp_err_t err = esp_http_client_perform(client);
		if (err == ESP_OK)
		{
			ESP_LOGI(TAG, "HTTP POST Status = %d, content_length = %d",
					esp_http_client_get_status_code(client),
					esp_http_client_get_content_length(client));

			volatile int read1 = esp_http_client_read(client, certificate_location, 10);
			volatile int read2 = esp_http_client_read(client, certificate_location+10, 10);

			ESP_LOGI(TAG, "Body: %s", certificate_location);

			cJSON *body = cJSON_Parse(certificate_location);
			if(body!=NULL){
				if(cJSON_HasObjectItem(body, "ver")){
					receivedVersion =  cJSON_GetObjectItem(body, "ver")->valueint;
					ESP_LOGW(TAG, "Received version: %d", receivedVersion);
				}
				if(cJSON_HasObjectItem(body, "data")){
					//strcpy(receivedSign, cJSON_GetObjectItem(body, "sign")->valuestring);
					ESP_LOGW(TAG, "Received cert len: %d", strlen(cJSON_GetObjectItem(body, "data")->valuestring));
				}

				if(cJSON_HasObjectItem(body, "sign")){
					strcpy(receivedSign, cJSON_GetObjectItem(body, "sign")->valuestring);
					ESP_LOGW(TAG, "Received signature: %s", receivedSign);
				}

				cJSON_Delete(body);
			}
			else
			{
				ESP_LOGW(TAG, "bad body");
			}
		}
		else
		{
			ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
		}

		esp_http_client_cleanup(client);
		free(certificate_location);
	}*/

	vTaskDelete(taskCertHandle);

}


/*static void certificate_stream_task()
{

//	char certificate_location[50000] = {0};
//	esp_http_client_config_t config = {
//		.url = certificate_location,
//		.cert_pem = (char *)server_cert_pem_start,
//		// .use_global_ca_store = true,
//		.event_handler = _http_event_handler,
//		.timeout_ms = 20000,
//		.buffer_size = 1536,
//	};

	bool newCertificateRequired = true;
	while(newCertificateRequired)
	{

		while((network_WifiIsConnected() == false) && (LteIsConnected() == false))
		{
			vTaskDelay(pdMS_TO_TICKS(1000));
		}


		char *buffer = calloc(1536,1);
		if (buffer == NULL) {
			ESP_LOGE(TAG, "Cannot malloc http receive buffer");
			return;
		}

		size_t free_dram = heap_caps_get_free_size(MALLOC_CAP_8BIT);
		size_t low_dram = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
		ESP_LOGE(TAG, "MEM1: DRAM: %i Lo: %i", free_dram, low_dram);

		char * url = "https://devices.zaptec.com/bundle";

		ESP_LOGI(TAG, "Get cert from: %s", url);

		//char certificate_location[1536] = {0};
		esp_http_client_config_t config = {
			.url = url,
			.transport_type = HTTP_TRANSPORT_OVER_SSL,
			.event_handler = _http_event_handler,
			.user_data = buffer,//certificate_location,
			.cert_pem = (char *)zap_cert_pem_start,
			.timeout_ms = 20000,
			.buffer_size = 1536,
		};

		esp_http_client_handle_t client = esp_http_client_init(&config);

		// POST
		int receivedVersion = 0;
		char receivedSign[200] = {0};

		char post_data [100] = {0};
		snprintf(post_data, 100,"{\"ver\":7, \"serial\": \"%s\"}", i2cReadDeviceInfoFromEEPROM().serialNumber);
		//char * post_data = "{\"ver\":7, \"serial\": \"}";

		esp_http_client_set_method(client, HTTP_METHOD_POST);
		esp_http_client_set_header(client, "Content-Type", "application/json");
		esp_http_client_set_post_field(client, post_data, strlen(post_data));



	    esp_err_t err;
	    int postlen = strlen(post_data);
	    if ((err = esp_http_client_open(client, postlen)) != ESP_OK) {
	        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
	        free(buffer);
	        return;
	    }
	    int content_length =  esp_http_client_fetch_headers(client);



	    int total_read_len = 0, read_len = 0;
	    if (total_read_len < content_length && content_length <= 1536) {
	        read_len = esp_http_client_read(client, buffer, content_length);
	        if (read_len <= 0) {
	            ESP_LOGE(TAG, "Error read data");
	        }
	        buffer[read_len] = 0;
	        ESP_LOGD(TAG, "read_len = %d", read_len);
	    }
	    ESP_LOGI(TAG, "HTTP Stream reader Status = %d, content_length = %d",
	                    esp_http_client_get_status_code(client),
	                    esp_http_client_get_content_length(client));






		//esp_err_t err = esp_http_client_perform(client);
		if (read_len > 0)
		{
//			ESP_LOGI(TAG, "HTTP POST Status = %d, content_length = %d",
//					esp_http_client_get_status_code(client),
//					esp_http_client_get_content_length(client));
//
//			volatile int read = esp_http_client_read(client, certificate_location, 1536);
//
//			ESP_LOGI(TAG, "Body: %s", certificate_location);

			cJSON *body = cJSON_Parse(buffer);
			if(body!=NULL){
				if(cJSON_HasObjectItem(body, "ver")){
					receivedVersion =  cJSON_GetObjectItem(body, "ver")->valueint;
					ESP_LOGW(TAG, "Received version: %d", receivedVersion);
				}

				if(cJSON_HasObjectItem(body, "sign")){
					strcpy(receivedSign, cJSON_GetObjectItem(body, "sign")->valuestring);
					ESP_LOGW(TAG, "Received signature: %s", receivedSign);
				}

				fat_WriteCertificate(body);

				fat_ReadCertificate();

				fat_static_unmount();

				cJSON_Delete(body);
			}
			else
			{
				ESP_LOGW(TAG, "bad body");
			}
		}
		else
		{
			ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
		}

		//esp_http_client_cleanup(client);

		esp_http_client_close(client);
		esp_http_client_cleanup(client);
		free(buffer);
	}

	vTaskDelete(taskCertHandle);

}*/



int certificateValidate()
{


	//hmac-sha256
	unsigned char mac[33] = "";

	int base64_key_len = 177;
	unsigned char keybuffer[base64_key_len];

	memcpy(keybuffer, zaptecPublicKey, base64_key_len);
	ESP_LOGI(TAG, "base64 key is: %s", keybuffer);

	volatile int data_len = 38667;//38632;
	//volatile int data_len2 = bundle_crt_end - bundle_crt_start;

	//volatile int x = hmac_sha256(keybuffer, base64_key_len, bundle_crt_start, data_len, mac);
	volatile int x = hmac_sha256(bundle_crt_start, data_len, NULL, NULL, mac);
	//volatile int x = hmac_sha256(NULL, NULL, bundle_crt_start, data_len, mac);

	//ESP_LOGI(TAG, "mac: %s", (char*)mac);

	printf("\r\n Hash: ");
	for (int i = 0; i < 31; i++)
		printf("0x%02X ", mac[i]);

	printf("\r\n");

    int err = 0;

    char * argc = 2;
    char * argv[] = {0};



    if(argc < 2) {
        printf("Usage: %s <public key file>\n", argv[0]);
        return 1;
    }

    // First, we have to load the public key
    mbedtls_pk_context key_ctx;
    mbedtls_pk_init(&key_ctx);

    //if((err = mbedtls_pk_parse_public_keyfile(&key_ctx, argv[1]))) {
    volatile size_t keylen = strlen((const char*)zaptecPublicKey) + 4;
    if((err = mbedtls_pk_parse_public_key(&key_ctx, zaptecPublicKey, keylen))) {
        printf("Public key read failed: %i\n", err);
        return 1;
    }

    // Assuming read key is EC public key
    mbedtls_ecdsa_context *ctx = (mbedtls_ecdsa_context*)key_ctx.pk_ctx;

    const char sig[] = "304402205882d3826fc16f9da91700ed715b5288736077dcc27a3968594aeb8765bf9c33022031e4127eb145d9eaf5177912ae2a7281820e9556d9ef144789372d4e6312a7c3";
    volatile char *pos = sig;
    unsigned char val[70] = {0};

    printf("\r\n");
	 /* WARNING: no sanitization or error-checking whatsoever */
	for (size_t count = 0; count < sizeof val/sizeof *val; count++) {
		sscanf(pos, "%2hhx", &val[count]);
		printf("0x%2X ", val[count]);
		pos += 2;
	}

	printf("\r\n");

    // Hard-coded SHA256-hash of certificate bundle (sha256("data")) + signature ("sig")
    const unsigned char hash[] = {0xf7, 0xe8, 0x05, 0x95, 0x43, 0xfc, 0x49, 0x94, 0xd0, 0xbf, 0x5f, 0x8f, 0x9c, 0x33, 0xb3, 0x4d, 0xcc, 0x59, 0xb5, 0xee, 0x74, 0xd3, 0xfe, 0x9a, 0x04, 0x97, 0x39, 0x6c, 0x65, 0x2d, 0xb6, 0x72};
    //const unsigned char sig[] = {0x30, 0x44, 0x02, 0x20, 0x0f, 0xd8, 0x06, 0x4a, 0xd1, 0xe4, 0x51, 0x8f, 0x0f, 0xb1, 0x76, 0x57, 0x00, 0x52, 0x22, 0xf0, 0xf7, 0x3b, 0x84, 0x9d, 0xc0, 0x07, 0xa6, 0x1c, 0x7b, 0xf6, 0xb0, 0x49, 0x91, 0x82, 0xdb, 0x2a, 0x02, 0x20, 0x43, 0x20, 0xae, 0x41, 0x95, 0x1e, 0xed, 0x88, 0x53, 0x62, 0x42, 0x37, 0xd3, 0x26, 0x3a, 0x5e, 0xd6, 0xc8, 0x4e, 0xbb, 0x82, 0x0d, 0x5a, 0xd0, 0x13, 0x39, 0xd5, 0xe9, 0xe9, 0x0d, 0xe8, 0x99};

    //int result = mbedtls_ecdsa_read_signature(ctx, hash, sizeof(hash), (unsigned char*)sig, sizeof(sig));
    int result = mbedtls_ecdsa_read_signature(ctx, hash, sizeof(hash), (unsigned char*)val, sizeof(val));

    mbedtls_pk_init(&key_ctx);

    if(result == 0) {
        printf("Signature verified\n");
        return 0;
    }
    else {
        printf("Verification failed: %X\n", -result);
        return 1;
    }

}


void certificate_init()
{
	//fat_make();

	fat_static_mount();

	//fat_WriteCertificate();

	//fat_ReadCertificate();

	//fat_static_unmount();

	/*esp_err_t err = esp_tls_init_global_ca_store();
	if(err != ESP_OK)
		printf("Creating store failed: %i\n", err);


	//err = esp_tls_set_global_ca_store(const unsigned char *cacert_pem_buf, const unsigned int cacert_pem_bytes);
	if(err != ESP_OK)
		printf("Creating store failed: %i\n", err);
*/
}
void certificate_update()
{
	xTaskCreate(certificate_task, "certificate_task", 8192, NULL, 2, &taskCertHandle);

}
