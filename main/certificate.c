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
#include "DeviceInfo.h"
#include "protocol_task.h"

#include "mbedtls/sha256.h"

#define u8 uint8_t

#include "../wpa_supplicant/src/crypto/sha256.h"

#define MAX_HTTP_RECV_BUFFER 		512


static const char *TAG = "CERT           ";

static TaskHandle_t taskCertHandle = NULL;
static bool certificateIsOk = false;
static bool taskRunning = false;

extern const uint8_t zap_cert_pem_start[] asm("_binary_zaptec_ca_cer_start");
extern const uint8_t zap_cert_pem_end[] asm("_binary_zaptec_ca_cer_end");

extern const uint8_t bundle_crt_start[] asm("_binary_bundle8_crt_start");
extern const uint8_t bundle_crt_end[] asm("_binary_bundle8_crt_end");

static const char zaptecPublicKey[] = "-----BEGIN PUBLIC KEY-----\nMFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEvTEC5cEvbSNkBksOwRItuhBUf3my\n7Eo0EO9Z784bTQ01PkUZcT5JnkFkGRVTzvLlMqNYZvZIGQLfkJqffSFMZA==\n-----END PUBLIC KEY-----\0";


static char * certificate = NULL;
static unsigned int certificateLength = 0;

static char sign[200] = {0};
static unsigned int signLength = 0;
char *certificate_bundle = NULL;

static bool useCertificateBundle = true;

void certificate_SetUsage(bool usage)
{
	useCertificateBundle = usage;
}

bool certificate_GetUsage()
{
	return useCertificateBundle;
}

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
                ESP_LOGW(TAG, "Last esp error code: 0x%x", err);
                ESP_LOGW(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
            }
            break;
    }
    return ESP_OK;
}


static int currentBundleVersion = 0;
uint8_t ParseCertificateBundle(char * certificateBundle)
{
	bool certificateValidated = false;

	cJSON *body = cJSON_Parse(certificateBundle);
	if(body!=NULL){
		if(cJSON_HasObjectItem(body, "ver")){
			currentBundleVersion =  cJSON_GetObjectItem(body, "ver")->valueint;
			ESP_LOGW(TAG, "Version: %d", currentBundleVersion);
		}
		else
		{
			//Do not continue if the bundle has no version number.
			return 1;
		}

		if(cJSON_HasObjectItem(body, "sign")){
				signLength = strlen(cJSON_GetObjectItem(body, "sign")->valuestring);
				memcpy(sign, cJSON_GetObjectItem(body, "sign")->valuestring, signLength);
				ESP_LOGW(TAG, "Signature: %s", sign);
		}
		else
		{
			//Do not continue if the bundle has no signature
			return 1;
		}

		if(cJSON_HasObjectItem(body, "data")){

			if(cJSON_GetObjectItem(body, "data")->valuestring != NULL)
				ESP_LOGW(TAG, "Cert len: %d", strlen(cJSON_GetObjectItem(body, "data")->valuestring));
			else
			{
				cJSON_Delete(body);
				ESP_LOGW(TAG, "data = NULL");
				return 2;
			}
		}
		else
		{
			//Do not continue if the bundle is empty - same version number as we already have
			cJSON_Delete(body);
			return 2;
		}


		certificateLength = strlen(cJSON_GetObjectItem(body, "data")->valuestring);
		memset(certificate,0, MAX_CERTIFICATE_SIZE);
		memcpy(certificate, cJSON_GetObjectItem(body, "data")->valuestring, certificateLength);

		cJSON_Delete(body);

		certificateValidated = certificateValidate();

		if(certificateValidated)
		{
			esp_err_t err = esp_tls_init_global_ca_store();
			if(err != ESP_OK)
				ESP_LOGE(TAG,"Creating store failed: %i", err);

			//err = esp_tls_set_global_ca_store(bundle_crt_start, bundle_crt_end - bundle_crt_start);
			err = esp_tls_set_global_ca_store((unsigned char*)certificate, certificateLength+1);
			if(err != ESP_OK)
			{
				certificateValidated = false;
				ESP_LOGE(TAG,"Creating store failed: %i", err);
			}
			else
			{
				ESP_LOGI(TAG, "Set new certificate in global ca");
			}
		}

	}

	if (certificateValidated)
		return 0;	//Valid certifcate
	else
		return 1;
}

void certifcate_setBundleVersion(int newBundleVersion)
{
	currentBundleVersion = newBundleVersion;
}

int certificate_GetCurrentBundleVersion()
{
	return currentBundleVersion;
}

static int overrideVersion = -1;
void certifcate_setOverrideVersion(int override)
{
	overrideVersion = override;
}

static bool hasReceivedNewCertificate = false;
bool certificate_CheckIfReceivedNew()
{
	return hasReceivedNewCertificate;
}

void certificate_task(void* tlsErrorCause)
{
	int tlsError = (int)tlsErrorCause;
	certificateIsOk = false;

	certificate_bundle = calloc(MAX_CERTIFICATE_BUNDLE_SIZE,1);

	uint32_t backoffDelay = 10;

	SetEspNotification(eNOTIFICATION_CERT_BUNDLE_REQUESTED);

	while(!certificateIsOk)
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

		//Certificate override is not used on this http config since it does not use the certificate bundle.

		esp_http_client_config_t config = {
			.url = url,
			.transport_type = HTTP_TRANSPORT_OVER_SSL,
			.event_handler = _http_event_handler,
			.user_data = certificate_bundle,
			.cert_pem = (char *)zap_cert_pem_start,//Use Zaptec Root CA when getting new certificate bundle
			.timeout_ms = 30000,
			.buffer_size = 1536,
		};

		esp_http_client_handle_t client = esp_http_client_init(&config);

		// POST
		char post_data [100] = {0};
		//snprintf(post_data, 100,"{\"ver\":6, \"serial\": \"%s\", \"fw\": \"%s\"}", i2cGetLoadedDeviceInfo().serialNumber, GetSoftwareVersion());
		if(overrideVersion > -1)
		{
			snprintf(post_data, 100,"{\"ver\":%d, \"serial\": \"%s\", \"fw\": \"%s\", \"error\": \"%d\", \"override\":%d }", currentBundleVersion, i2cGetLoadedDeviceInfo().serialNumber, GetSoftwareVersion(), tlsError, overrideVersion);
		}
		else
		{
			snprintf(post_data, 100,"{\"ver\":%d, \"serial\": \"%s\", \"fw\": \"%s\", \"error\": \"%d\"}", currentBundleVersion, i2cGetLoadedDeviceInfo().serialNumber, GetSoftwareVersion(), tlsError);
		}

		overrideVersion = -1;

		ESP_LOGW(TAG, "post_data: %s", post_data);

		//snprintf(post_data, 100,"{\"ver\":6, \"serial\": \"%s\"}", i2cGetLoadedDeviceInfo().serialNumber);
		//char * post_data = "{\"ver\":7, \"serial\": \"}";

		esp_http_client_set_method(client, HTTP_METHOD_POST);
		esp_http_client_set_header(client, "Content-Type", "application/json");

		int postlen = strlen(post_data);
		volatile esp_err_t err;
		if ((err = esp_http_client_open(client, postlen)) != ESP_OK) {
			ESP_LOGE(TAG, "Failed to open HTTP connection: %s, backing off: %d sec", esp_err_to_name(err), backoffDelay);


			//Make sure it is trying more and more seldom, but max
			vTaskDelay(pdMS_TO_TICKS(1000 * backoffDelay));
			if(backoffDelay < (3600 * 6)) //Backoff maximum 6 hours
				backoffDelay += 10;

			esp_http_client_close(client);
			esp_http_client_cleanup(client);

			continue;
		}

		err = esp_http_client_write(client, post_data, postlen);

		esp_http_client_fetch_headers(client);
		volatile int read_len = 0;

		read_len = esp_http_client_read(client, certificate_bundle, MAX_CERTIFICATE_BUNDLE_SIZE);

		esp_http_client_close(client);
		esp_http_client_cleanup(client);

		if(read_len == 0)
		{
			ESP_LOGE(TAG, "Did not get any data -  backing off: %d sec", backoffDelay);

			vTaskDelay(pdMS_TO_TICKS(1000 * backoffDelay));
			if(backoffDelay < (3600 * 6)) //Backoff maximum 6 hours
				backoffDelay += 10;

			continue;
		}


		ESP_LOGW(TAG, "Len: %d, Body: %c%c%c ... %c%c%c  ", strlen(certificate_bundle), certificate_bundle[0], certificate_bundle[1], certificate_bundle[2], certificate_bundle[read_len-3], certificate_bundle[read_len-2], certificate_bundle[read_len-1]);

		uint8_t certStatus = ParseCertificateBundle(certificate_bundle);

		if(certStatus == 0) //Valid certifcate
		{
			fat_WriteCertificateBundle(certificate_bundle);

			memset(certificate_bundle, 0, MAX_CERTIFICATE_BUNDLE_SIZE);

			fat_ReadCertificateBundle(certificate_bundle);

			ESP_LOGW(TAG, "Len: %d, Body: %c%c%c ... %c%c%c  ", strlen(certificate_bundle), certificate_bundle[0], certificate_bundle[1], certificate_bundle[2], certificate_bundle[read_len-3], certificate_bundle[read_len-2], certificate_bundle[read_len-1]);

			free(certificate_bundle);
			certificateIsOk = true;
			hasReceivedNewCertificate = true;
		}
		else if(certStatus == 1) //Not a valid header - back off and try again
		{
			ESP_LOGE(TAG, "Did not get a valid certificate - backing off: %d sec", backoffDelay);

			vTaskDelay(pdMS_TO_TICKS(1000 * backoffDelay));
			if(backoffDelay < (3600 * 6)) //Backoff maximum 6 hours
				backoffDelay += 60;

			continue;
		}
		else if(certStatus == 2)
		{
			ESP_LOGE(TAG, "Valid data, but no header, we have the right version, exit");
			break;
		}

		//esp_http_client_close(client);
		//esp_http_client_cleanup(client);

	}

	ESP_LOGI(TAG, "Ending thread");

	taskRunning = false;

	vTaskDelete(taskCertHandle);
}


bool certificateValidate()
{
	mbedtls_sha256_context sha256_ctx;

	unsigned char sha256[32];

	mbedtls_sha256_init(&sha256_ctx);

	mbedtls_sha256_starts_ret(&sha256_ctx, false);

	mbedtls_sha256_update_ret(&sha256_ctx, (unsigned char*)certificate, certificateLength);

	mbedtls_sha256_finish_ret(&sha256_ctx, sha256);

	mbedtls_sha256_free(&sha256_ctx);

	/*printf("\r\n Hash1: ");
	for (int i = 0; i <= 31; i++)
		printf("0x%02X ", sha256[i]);
	*/

	int base64_key_len = strlen(zaptecPublicKey)+1;

    // First, we have to load the public key
    mbedtls_pk_context key_ctx;
    mbedtls_pk_init(&key_ctx);

    esp_err_t err;
    if((err = mbedtls_pk_parse_public_key(&key_ctx, (unsigned char*)zaptecPublicKey, base64_key_len))) {
    	ESP_LOGE(TAG,"Public key read failed: %i", err);
        return false;
    }

    // Assuming read key is EC public key
    mbedtls_ecdsa_context *ctx = (mbedtls_ecdsa_context*)key_ctx.pk_ctx;

    char *pos = sign;
    unsigned char signBytes[100] = {0};

    //printf("\r\n signBytes: ");

    int nrOfSignBytes = signLength / 2;

    size_t count = 0;
	 /* WARNING: no sanitization or error-checking whatsoever */
    for (count = 0; count < nrOfSignBytes; count++) {
		sscanf(pos, "%2hhx", &signBytes[count]);
		//printf("%02X", signBytes[count]);
		pos += 2;
	}

	//printf("\r\n");

    // Hard-coded SHA256-hash of certificate bundle (sha256("data")) + signature ("sig")
    //const unsigned char hash[] = {0xf7, 0xe8, 0x05, 0x95, 0x43, 0xfc, 0x49, 0x94, 0xd0, 0xbf, 0x5f, 0x8f, 0x9c, 0x33, 0xb3, 0x4d, 0xcc, 0x59, 0xb5, 0xee, 0x74, 0xd3, 0xfe, 0x9a, 0x04, 0x97, 0x39, 0x6c, 0x65, 0x2d, 0xb6, 0x72};
    //const unsigned char sig[] = {0x30, 0x44, 0x02, 0x20, 0x0f, 0xd8, 0x06, 0x4a, 0xd1, 0xe4, 0x51, 0x8f, 0x0f, 0xb1, 0x76, 0x57, 0x00, 0x52, 0x22, 0xf0, 0xf7, 0x3b, 0x84, 0x9d, 0xc0, 0x07, 0xa6, 0x1c, 0x7b, 0xf6, 0xb0, 0x49, 0x91, 0x82, 0xdb, 0x2a, 0x02, 0x20, 0x43, 0x20, 0xae, 0x41, 0x95, 0x1e, 0xed, 0x88, 0x53, 0x62, 0x42, 0x37, 0xd3, 0x26, 0x3a, 0x5e, 0xd6, 0xc8, 0x4e, 0xbb, 0x82, 0x0d, 0x5a, 0xd0, 0x13, 0x39, 0xd5, 0xe9, 0xe9, 0x0d, 0xe8, 0x99};

    int result = mbedtls_ecdsa_read_signature(ctx, sha256, sizeof(sha256), signBytes, nrOfSignBytes);

    mbedtls_pk_init(&key_ctx);

    if(result == 0) {
    	ESP_LOGI(TAG, "Signature verified");
        return true;
    }
    else {
    	ESP_LOGE(TAG,"Verification failed: %X", -result);
        return false;
    }
}


void certificate_init()
{
	certificate = calloc(MAX_CERTIFICATE_SIZE,1); //This is used for global ca buffer - do not need to free it.

	if(fatIsMounted())
	{
		certificate_bundle = calloc(MAX_CERTIFICATE_BUNDLE_SIZE,1); //Must be free'ed

		//Read certificate from flash
		fat_ReadCertificateBundle(certificate_bundle);

		//Parse and validate
		uint8_t certStatus = ParseCertificateBundle(certificate_bundle);

		if(certStatus == 1) //Invalid header
		{
			free(certificate_bundle);
			ESP_LOGW(TAG, "Missing or invalid certificate, connect to server and get new");
			certificate_update(1);
		}
		else if(certStatus == 0) //Valid
		{
			free(certificate_bundle);
			ESP_LOGW(TAG, "Found and set valid certificate");
			certificateIsOk = true;
		}
		else if(certStatus == 2)
		{
			ESP_LOGW(TAG, "Valid header, no data");
			certificateIsOk = true; //TODO: check this behavior with no data
		}
	}
	else
	{
		ESP_LOGW(TAG,"Using buildtin certificate");
		//Fallback to included certificate if FAT partition can't be mounted. Can reduce application size if this is not needed.
		esp_err_t err = esp_tls_init_global_ca_store();
		if(err != ESP_OK)
			ESP_LOGE(TAG,"Creating store failed: %i", err);

		err = esp_tls_set_global_ca_store(bundle_crt_start, bundle_crt_end - bundle_crt_start);
		if(err != ESP_OK)
		{
			certificateIsOk = false;
			ESP_LOGE(TAG,"Creating store for included certificate failed: %i", err);
		}
		else
		{
			ESP_LOGE(TAG, "##### Set INCLUDED certificate in global ca #####");
			certificateIsOk = true;
		}
	}

}

bool certificateOk()
{
	return certificateIsOk;
}

void certificate_clear()
{
	fat_DeleteCertificateBundle();
	memset(certificate, 0, MAX_CERTIFICATE_SIZE);
	esp_tls_free_global_ca_store();
	esp_tls_init_global_ca_store();
	ESP_LOGE(TAG, "Certificate cleared");
}



void certificate_update(int tls_error)
{
	//Only allow one instance
	if(taskRunning == false)
	{
		taskRunning = true;
		xTaskCreate(certificate_task, "certificate_task", 8192, (void*)tls_error, 2, &taskCertHandle);
	}

}
