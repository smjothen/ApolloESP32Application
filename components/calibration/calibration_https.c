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
#include "zaptec_cloud_observations.h"

#include "mbedtls/sha256.h"

#include "calibration.h"

static const char *TAG = "CALIBRATION    ";

static char buf[1024];

extern const uint8_t zap_cert_pem_start[] asm("_binary_zaptec_ca_cer_start");
extern const uint8_t zap_cert_pem_end[] asm("_binary_zaptec_ca_cer_end");

bool calibration_https_upload_to_cloud(CalibrationCtx *ctx, const char *raw) {

		memset(buf, 0, sizeof (buf));

		cJSON *calibration = cJSON_CreateObject();

		CalibrationParameter *params[] = {
				ctx->Params.CurrentGain,
				ctx->Params.VoltageGain,
				ctx->Params.CurrentOffset,
				ctx->Params.VoltageOffset,
		};

		for (size_t i = 0; i < sizeof (params) / sizeof (params[0]); i++) {
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

		cJSON *data = cJSON_CreateObject();

		cJSON_AddItemToObject(data, "Calibration", calibration);
		cJSON_AddStringToObject(data, "Raw", raw);

		if (!cJSON_PrintPreallocated(data, buf, sizeof (buf), true)) {
			ESP_LOGE(TAG, "Unable to publish JSON calibration data");
		} else {
			ESP_LOGI(TAG, "Publishing JSON calibration data");

			// Quickly enable cloud observations for uploading calibration data
			cloud_observations_disable(false);
			publish_debug_telemetry_observation_Calibration(buf);
			cloud_observations_disable(true);
		}

		return true;
}

// Uploads parameters and sets the calibration ID received from the production
// server
bool calibration_https_upload_parameters(CalibrationCtx *ctx, const char *raw, bool verification) {

		memset(buf, 0, sizeof (buf));

#ifdef CONFIG_CAL_SIMULATION_PROD_SERV
		ESP_LOGI(TAG, "Simulating production server data transfer, using calibration ID 1337!");
		ctx->Params.CalibrationId = 1337;
		return true;
#endif

    char *url = "https://devices.zaptec.com/production/mid/calibration";
		if (verification) {
    	url = "https://devices.zaptec.com/production/mid/verification";
		}

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

		size_t data_len = 0;
		char *data_str = NULL;

		cJSON *calibration = cJSON_CreateObject();

		CalibrationParameter *params[] = {
				ctx->Params.CurrentGain,
				ctx->Params.VoltageGain,
				ctx->Params.CurrentOffset,
				ctx->Params.VoltageOffset,
		};

		const char *paramNames[] = {
			"CurrentGain",
			"VoltageGain",
			"CurrentOffset",
			"VoltageOffset",
		};

		for (size_t i = 0; i < sizeof (params) / sizeof (params[0]); i++) {
			bool hasParam = true;
			for (int j = 0; j < 3; j++) {
				if (!params[i][j].assigned) {
					hasParam = false;
				}
			}

			const char *key = paramNames[i];

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

		cJSON *verifications = cJSON_CreateObject();

		CalibrationParameter *verifs[] = {
			&ctx->Verifs.Verification[I_min_go],
		};

		const char *verifNames[] = {
			"I_min_go"
		};

		for (size_t i = 0; i < sizeof (verifs) / sizeof (verifs[0]); i++) {
			bool hasParam = verifs[i]->assigned;
			const char *key = verifNames[i];

			if (hasParam) {
				cJSON_AddNumberToObject(verifications, key, verifs[i]->value);
			}
		}

		cJSON *test = cJSON_CreateObject();
		cJSON_AddNumberToObject(test, "Station", ctx->Position);
		cJSON_AddNumberToObject(test, "Run", ctx->Run);
#ifdef CONFIG_CAL_SIMULATION
		cJSON_AddBoolToObject(test, "IsSimulated", true);
#else
		cJSON_AddBoolToObject(test, "IsSimulated", false);
#endif

		if (verification) {
			cJSON_AddNumberToObject(test, "CalibrationId", ctx->Params.CalibrationId);
		}

		cJSON *parameters = cJSON_CreateObject();
		cJSON_AddItemToObject(parameters, "Calibration", calibration);

		if (verification) {
			cJSON_AddItemToObject(parameters, "Verifications", verifications);
		}

		cJSON_AddItemToObject(parameters, "Test", test);

		cJSON *data = cJSON_CreateObject();
		cJSON_AddStringToObject(data, "serial", devInfo.serialNumber);

		if (!verification) {
			cJSON_AddStringToObject(data, "raw", raw);
		}

		if (verification) {
			// If we'ves gotten this far into Done state, we should've passed?
			cJSON_AddBoolToObject(data, "pass", true);

			// TODO: Add more??
			cJSON *runInfo = cJSON_CreateObject();
			cJSON_AddStringToObject(runInfo, "FirmwareVersion", GetSoftwareVersion());

			char *run_str = cJSON_PrintUnformatted(runInfo);
			cJSON_AddStringToObject(data, "runInfo", run_str);

			cJSON_Delete(runInfo);
		}

		char *param_str = cJSON_PrintUnformatted(parameters);

		cJSON_AddStringToObject(data,
				verification ? "parametersAndVerifications" : "parameters",
				param_str);

		data_str = cJSON_PrintUnformatted(data);
		data_len = strlen(data_str);

		cJSON_Delete(parameters);
		cJSON_Delete(data);
		free(param_str);

		ESP_LOGI(TAG, "Uploading: %s", data_str);

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

		free(data_str);

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

		if (verification) {
			cJSON *body = cJSON_Parse(buf);
			if (!body) {
				ESP_LOGE(TAG, "Invalid verification response!");
				return false;
			}
	
			if (!cJSON_HasObjectItem(body, "pass")) {
				ESP_LOGE(TAG, "No calibration pass value in response!");

				cJSON_Delete(body);
				body = NULL;

				return false;
			}

			bool pass = cJSON_GetObjectItem(body, "pass")->valueint;
			if (!pass) {
				ESP_LOGE(TAG, "Server pass failed!");
			}

			cJSON_Delete(body);

			return pass;
		} else {
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
		}

    return true;
}
