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

#include "calibration.h"

static const char *TAG = "CALIBRATION    ";

static char buf[1024];

extern const uint8_t zap_cert_pem_start[] asm("_binary_zaptec_ca_cer_start");
extern const uint8_t zap_cert_pem_end[] asm("_binary_zaptec_ca_cer_end");

// Uploads parameters and sets the calibration ID received from the production
// server
bool calibration_https_upload_parameters(CalibrationCtx *ctx, const char *raw) {
    char *url = "https://devices.zaptec.com/production/mid/calibration";

		esp_http_client_config_t config = {
			.url = url,
			.transport_type = HTTP_TRANSPORT_OVER_SSL,
			.event_handler = NULL,
			.user_data = buf,
			.cert_pem = (char *)zap_cert_pem_start,
			.timeout_ms = 30000,
			.buffer_size = 1536,
		};

		esp_http_client_handle_t client = esp_http_client_init(&config);

    struct DeviceInfo devInfo = i2cGetLoadedDeviceInfo();

		cJSON *test = cJSON_CreateObject();
		cJSON_AddNumberToObject(test, "Station", ctx->Position);
		cJSON_AddNumberToObject(test, "Run", ctx->Run);
#ifdef CALIBRATION_SIMULATION
		cJSON_AddBoolToObject(test, "IsSimulated", true);
#else
		cJSON_AddBoolToObject(test, "IsSimulated", false);
#endif

		cJSON *calibration = cJSON_CreateObject();

    CalibrationParameter *params[] = {
        ctx->Params.CurrentGain,
        ctx->Params.VoltageGain,
        ctx->Params.CurrentOffset,
        ctx->Params.VoltageOffset,
    };

		for (int i = 0; i < 4; i++) {
			bool hasParam = true;
			for (int j = 0; j < 3; j++) {
				if (!params[i][j].assigned) {
					hasParam = false;
				}
			}

			const char *key = i == 0 ? "CurrentGain" :
				i == 1 ? "VoltageGain" :
				i == 2 ? "CurrentOffset" : "VoltageOffset";

			if (hasParam) {
				cJSON *param = cJSON_CreateObject();
				for (int j = 0; j < 3; j++) {
					cJSON_AddNumberToObject(param, 
							j == 0 ? "0" : j == 1 ? "1" : "2",
							params[i][j].value);
				}
				cJSON_AddItemToObject(calibration, key, param);
			}
		}

		cJSON *parameters = cJSON_CreateObject();
		cJSON_AddItemToObject(parameters, "Calibration", calibration);
		cJSON_AddItemToObject(parameters, "Test", test);

		cJSON *data = cJSON_CreateObject();
		cJSON_AddStringToObject(data, "serial", devInfo.serialNumber);
		cJSON_AddStringToObject(data, "raw", raw);

		const char *param_str = cJSON_PrintUnformatted(parameters);
		cJSON_AddStringToObject(data, "parameters", param_str);

		const char *data_str = cJSON_PrintUnformatted(data);
		size_t data_len = strlen(data_str);

		ESP_LOGI(TAG, "Uploading: %s", data_str);

		// TODO: Build me with cJSON
    // char *data = "{\"serial\": \"ZAP000000\", \"parameters\": \"\", \"raw\": \"\"}";
    // size_t data_len = strlen(data);

		esp_http_client_set_method(client, HTTP_METHOD_POST);
		esp_http_client_set_header(client, "Content-Type", "application/json");

		esp_err_t err;

		if ((err = esp_http_client_open(client, data_len)) != ESP_OK) {
			ESP_LOGE(TAG, "Error uploading parameters: %s", esp_err_to_name(err));

			esp_http_client_close(client);
			esp_http_client_cleanup(client);

      return false;
		}

		err = esp_http_client_write(client, data_str, data_len);
		if (err == ESP_FAIL) {
			ESP_LOGE(TAG, "Error uploading parameters: %s", esp_err_to_name(err));

			esp_http_client_close(client);
			esp_http_client_cleanup(client);

			return false;
		}

		esp_http_client_fetch_headers(client);

		int read_len = esp_http_client_read(client, buf, sizeof (buf));

		esp_http_client_close(client);
		esp_http_client_cleanup(client);

		if(read_len <= 0) {
			ESP_LOGE(TAG, "Didn't get any data from production server!");
      return false;
		}

		ESP_LOGI(TAG, "Got response from production server: %s", buf);

		cJSON *body = cJSON_Parse(buf);
		if (!body) {
			ESP_LOGE(TAG, "Invalid calibration response!");
			return false;
		}
		
		if (!cJSON_HasObjectItem(body, "CalibrationId")) {
			ESP_LOGE(TAG, "No calibration ID in response!");

			cJSON_Delete(body);
			body = NULL;

			return false;
		}

		ctx->Params.CalibrationId = cJSON_GetObjectItem(body, "CalibrationId")->valueint;
		ESP_LOGI(TAG, "Got calibration ID %d!", ctx->Params.CalibrationId);

		cJSON_Delete(body);
		body = NULL;

    return true;
}
