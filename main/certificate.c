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

static const char *TAG = "CERT    ";

//extern const uint8_t public_key_start[] asm("_binary_dspic_bin_start");
//extern const uint8_t dspic_bin_end[] asm("_binary_dspic_bin_end");

extern const uint8_t zap_cert_pem_start[] asm("_binary_zaptec_ca_cer_start");
extern const uint8_t zap_cert_pem_end[] asm("_binary_zaptec_ca_cer_end");

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
                ESP_LOG_BUFFER_HEX(TAG, output_buffer, output_len);
                free(output_buffer);
                output_buffer = NULL;
            }
            output_len = 0;
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            int mbedtls_err = 0;
            esp_err_t err = 0;
            // esp_err_t err = esp_tls_get_and_clear_last_error(evt->data, &mbedtls_err, NULL);
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


int certificateGetNew()
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

	while(network_WifiIsConnected() == false)
	{
		vTaskDelay(pdMS_TO_TICKS(1000));
	}

	size_t free_dram = heap_caps_get_free_size(MALLOC_CAP_8BIT);
	size_t low_dram = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
	ESP_LOGE(TAG, "MEM1: DRAM: %i Lo: %i", free_dram, low_dram);

	char * url = "https://devices.zaptec.com/bundle";

	ESP_LOGI(TAG, "Get cert from: %s", url);

	char certificate_location[1000] = {0};
	esp_http_client_config_t config = {
		.url = url,
		//.host = "httpbin.org",
		//.path = "/get",
		//.query = "esp",
		.event_handler = _http_event_handler,
		.user_data = certificate_location,
		.cert_pem = (char *)zap_cert_pem_start,
		.timeout_ms = 20000,
		//.buffer_size = 1536,
	};

	esp_http_client_handle_t client = esp_http_client_init(&config);

	// POST
	int receivedVersion = 0;
	char receivedSign[200] = {0};

	//char post_data [100] = {0};
	//snprintf(post_data, 100,"{\"ver\":7}");
	char * post_data = "{\"ver\":7}";

	esp_http_client_set_method(client, HTTP_METHOD_POST);
	esp_http_client_set_header(client, "Content-Type", "application/json");
	esp_http_client_set_post_field(client, post_data, strlen(post_data));

	esp_err_t err = esp_http_client_perform(client);
	if (err == ESP_OK)
	{
		ESP_LOGI(TAG, "HTTP POST Status = %d, content_length = %d",
				esp_http_client_get_status_code(client),
				esp_http_client_get_content_length(client));
		//ESP_LOGI(TAG, "Body: %s", local_response_buffer);
		cJSON *body = cJSON_Parse(certificate_location);
		if(body!=NULL){
			if(cJSON_HasObjectItem(body, "ver")){
				receivedVersion =  cJSON_GetObjectItem(body, "ver")->valueint;
				ESP_LOGW(TAG, "Received version: %d", receivedVersion);
			}

			if(cJSON_HasObjectItem(body, "data")){
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

	return ESP_OK;
}


int certificateValidate()
{
    int err = 0;

    char * argc = 2;
    char * argv[] = {0};

    const unsigned char pkey[] = "-----BEGIN PUBLIC KEY-----\nMFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEvTEC5cEvbSNkBksOwRItuhBUf3my\n7Eo0EO9Z784bTQ01PkUZcT5JnkFkGRVTzvLlMqNYZvZIGQLfkJqffSFMZA==\n-----END PUBLIC KEY-----\0";



    if(argc < 2) {
        printf("Usage: %s <public key file>\n", argv[0]);
        return 1;
    }

    // First, we have to load the public key
    mbedtls_pk_context key_ctx;
    mbedtls_pk_init(&key_ctx);

    //if((err = mbedtls_pk_parse_public_keyfile(&key_ctx, argv[1]))) {
    volatile size_t keylen = 181;//strlen((char)pkey);
    if((err = mbedtls_pk_parse_public_key(&key_ctx, pkey, keylen))) {
        printf("Public key read failed: %i\n", err);
        return 1;
    }

    // Assuming read key is EC public key
    mbedtls_ecdsa_context *ctx = (mbedtls_ecdsa_context*)key_ctx.pk_ctx;

    const char sig[] = "304402205882d3826fc16f9da91700ed715b5288736077dcc27a3968594aeb8765bf9c33022031e4127eb145d9eaf5177912ae2a7281820e9556d9ef144789372d4e6312a7c3";
    //const char sig[] = "DEadbeef10203040b00b1e50";
    volatile char *pos = sig;
	//unsigned char val[12];
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


