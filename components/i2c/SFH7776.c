
#include <stdio.h>

#include "esp_log.h"

#include "driver/i2c.h"

#include "i2cInterface.h"
#include "SFH7776.h"

static const char * TAG = "SFH7776";
static uint8_t i2c_addr = 0x39;

esp_err_t SFH7776_read_system_control(){
	return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t SFH7776_set_register(uint8_t reg, uint8_t value){
	uint8_t data[2];
	data[0] = reg;
	data[1] = value;
	return i2c_master_write_slave(i2c_addr, data, 2);

}

esp_err_t SFH7776_get_register(uint8_t reg, uint8_t * value){
	esp_err_t err = i2c_master_write_slave(i2c_addr, &reg, 1);
	if(err != ESP_OK)
		return err;

	return i2c_master_read_slave(i2c_addr, value, 1);
}

esp_err_t SFH7776_read_lsb_then_msb(uint8_t lsb_addr, uint16_t * value){
	esp_err_t err = i2c_master_write_slave(i2c_addr, &lsb_addr, 1);
	if(err != ESP_OK)
		return err;

	uint8_t buffer[2] = {0};
	err = i2c_master_read_slave(i2c_addr, buffer, 2);
	*value = (uint16_t)buffer[1] << 8 | buffer[0];
	return err;
}

esp_err_t SFH7776_test(){
	ESP_LOGE(TAG, "START TESTING.");
	if(SFH7776_set_sensor_control(0x1f) != ESP_OK)
		goto error;

	uint8_t ctrl = 0;
	if(SFH7776_get_sensor_control(&ctrl) != ESP_OK)
		goto error;

	if(ctrl != 0x1f){
		ESP_LOGE(TAG, "Sensor control set incorrectly. Expected 0x1f got %0x", ctrl);
		goto error;
	}

	if(SFH7776_set_mode_control(0x0b) != ESP_OK)
		goto error;

	if(SFH7776_get_sensor_control(&ctrl) != ESP_OK)
		goto error;

	if(ctrl != 0x0b){
		ESP_LOGE(TAG, "Sensor control set incorrectly. Expected 0x1f got %0x", ctrl);
		goto error;
	}

	for(uint8_t i = 0; i < 10; i++){
		vTaskDelay(pdMS_TO_TICKS(2000));

		uint16_t proximity;
		if(SFH7776_read_proximity(&proximity) != ESP_OK){
			goto error;
		}
		ESP_LOGW(TAG, "Proximity: %d", proximity);

		uint16_t ambient_light;
		if(SFH7776_read_ambient_light_visibile(&ambient_light) != ESP_OK){
			goto error;
		}
		ESP_LOGW(TAG, "Ambient_light (visible): %d", ambient_light);

		if(SFH7776_read_ambient_light_ir(&ambient_light) != ESP_OK){
			goto error;
		}
		ESP_LOGW(TAG, "Ambient light (IR): %d", ambient_light);

	}

	return ESP_OK;
error:
	return ESP_FAIL;
}
