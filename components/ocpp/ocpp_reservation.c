#include <string.h>
#include <errno.h>
#include <sys/stat.h>

#include <esp_log.h>
#include "esp_crc.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "ocpp_reservation.h"

static struct ocpp_reservation_info * current_reservation = NULL;
static const char * reservation_path = CONFIG_OCPP_FILE_PATH "/res.bin";
static const char * TAG = "OCPP RESERVED  ";
static SemaphoreHandle_t file_lock = NULL;


static bool filesystem_is_ready(){
	if(file_lock == NULL){
		ESP_LOGE(TAG, "File lock was not initialized");
		return false;
	}

	struct stat st;
	if(stat(CONFIG_OCPP_FILE_PATH, &st) != 0){
		ESP_LOGE(TAG, "Directory does not exist");
		return false;
	}

	return true;
}

static esp_err_t clear_info(){
	ESP_LOGE(TAG, "******** CLEARING RESERVATION *********");
	free(current_reservation);
	current_reservation = NULL;

	int err = remove(reservation_path);
	if(err != 0){
		ESP_LOGW(TAG, "Failed to remove: %s", strerror(errno));
		return ESP_FAIL;
	}

	return ESP_OK;
}

static esp_err_t read_reservation(FILE * fp, struct ocpp_reservation_info * reservation_out){
	if(fread(reservation_out, sizeof(struct ocpp_reservation_info), 1, fp) != 1){
		ESP_LOGE(TAG, "Unable to read reservation: %s", strerror(errno));
		return ESP_FAIL;
	}

	uint32_t crc;
	if(fread(&crc, sizeof(uint32_t), 1, fp) != 1){
		ESP_LOGE(TAG, "Unable to read crc: %s", strerror(errno));
		return ESP_FAIL;
	}

	if(crc != esp_crc32_le(0, (uint8_t *)reservation_out, sizeof(struct ocpp_reservation_info))){
		ESP_LOGE(TAG, "CRC mismatch when reading reservation");
		return ESP_ERR_INVALID_CRC;
	}

	return ESP_OK;
}

static esp_err_t write_reservation(FILE * fp, struct ocpp_reservation_info * reservation){
	if(fwrite(reservation, sizeof(struct ocpp_reservation_info), 1, fp) != 1){
		ESP_LOGE(TAG, "Unable to write reservation: %s", strerror(errno));
		return ESP_FAIL;
	}

	uint32_t crc = esp_crc32_le(0, (uint8_t *)reservation, sizeof(struct ocpp_reservation_info));
	if(fwrite(&crc, sizeof(uint32_t), 1, fp) != 1){
		ESP_LOGE(TAG, "Unable to write crc: %s", strerror(errno));
		return ESP_FAIL;
	}

	return ESP_OK;
}

esp_err_t ocpp_reservation_clear_info(){
	if(!filesystem_is_ready())
		return ESP_ERR_INVALID_STATE;

	if(xSemaphoreTake(file_lock, pdMS_TO_TICKS(2000)) != pdTRUE){
		ESP_LOGE(TAG, "Unable to aquire file lock to clear info");
		return ESP_FAIL;
	}

	ESP_LOGW(TAG, "Requested to clear reservation");
	esp_err_t err = clear_info();

	xSemaphoreGive(file_lock);
	return err;
}

esp_err_t ocpp_reservation_set_info(struct ocpp_reservation_info * info){
	if(!filesystem_is_ready())
		return ESP_ERR_INVALID_STATE;

	if(xSemaphoreTake(file_lock, pdMS_TO_TICKS(2000)) != pdTRUE){
		ESP_LOGE(TAG, "Unable to aquire file lock to clear info");
		return ESP_FAIL;
	}

	ESP_LOGI(TAG, "Setting reservation");

	esp_err_t err = ESP_FAIL;

	if(current_reservation == NULL){
		current_reservation = malloc(sizeof(struct ocpp_reservation_info));
		if(current_reservation == NULL){
			ESP_LOGE(TAG, "Unable to aquire memory for reservation during set_info");
			goto error;
		}
	}

	FILE * fp = fopen(reservation_path, "wb");
	if(fp == NULL){
		ESP_LOGE(TAG, "Unable to open reservation file: %s. Clearing info", strerror(errno));
		clear_info();
		goto error;
	}

	memcpy(current_reservation, info, sizeof(struct ocpp_reservation_info));
	err = write_reservation(fp, current_reservation);
	fclose(fp);
	if(err != ESP_OK){
		ESP_LOGE(TAG, "Unable to write reservation info during set_info. Clearing info");
		clear_info();
		goto error;
	}

	ESP_LOGI(TAG, "Setting reservation completed");

error:
	xSemaphoreGive(file_lock);

	return err;
}

struct ocpp_reservation_info * ocpp_reservation_get_info(){

	bool locked = false;

	if(current_reservation == NULL && file_lock != NULL){
		struct stat st;
		if(filesystem_is_ready() && stat(reservation_path, &st) == 0){
			if(xSemaphoreTake(file_lock, pdMS_TO_TICKS(2000)) != pdTRUE){
				ESP_LOGE(TAG, "Failed to aquire file lock within timeput");
				goto error;
			}
			locked = true;

			current_reservation = malloc(sizeof(struct ocpp_reservation_info));
			if(current_reservation == NULL){
				ESP_LOGE(TAG, "Could not aquire memory for reservation");
				goto error;
			}

			FILE * fp = fopen(reservation_path, "rb");
			if(fp == NULL){
				ESP_LOGE(TAG, "Unable to open existing reservation: %s", strerror(errno));
				goto error;
			}

			esp_err_t err = read_reservation(fp, current_reservation);
			fclose(fp);
			if(err != ESP_OK){
				ESP_LOGE(TAG, "Failed reservation read during get_info. Will clear info");
				clear_info();
				goto error;
			}
		}
	}

	if(current_reservation != NULL && current_reservation->expiry_date < time(NULL))
		clear_info();

error:
	if(locked)
		xSemaphoreGive(file_lock);

	return current_reservation;
}

cJSON * ocpp_reservation_get_diagnostics(){
	cJSON * res = cJSON_CreateObject();
	if(res == NULL){
		ESP_LOGE(TAG, "Unable to create ocpp diagnostics for reservation");
		return res;
	}

	struct stat st;
	int stat_res = stat(reservation_path, &st);

	cJSON_AddBoolToObject(res, "file_lock_exist", file_lock != NULL);
	cJSON_AddStringToObject(res, "file_stat_result", stat_res == 0 ? "exists" : strerror(errno));
	cJSON_AddBoolToObject(res, "current_reservation_exists", current_reservation != NULL);

	return res;
}

esp_err_t ocpp_reservation_init(){

	if(file_lock != NULL){
		ESP_LOGW(TAG, "Reservation already initialized");
		return ESP_ERR_INVALID_STATE;
	}

	file_lock = xSemaphoreCreateMutex();
	if(file_lock == NULL){
		ESP_LOGE(TAG, "Unable to create file lock");
		return ESP_FAIL;
	}

	return ESP_OK;
}

esp_err_t ocpp_reservation_deinit(){

	if(file_lock == NULL){
		ESP_LOGW(TAG, "Requested to deinit reservation, but reservation was not initialized");
		return ESP_ERR_INVALID_STATE;
	}

	SemaphoreHandle_t tmp_lock = file_lock;
	file_lock = NULL;

	struct ocpp_reservation_info * tmp_reservation = current_reservation;
	current_reservation = NULL;

	free(tmp_reservation);
	vSemaphoreDelete(tmp_lock);

	return ESP_OK;
}
