#include <stdio.h>
#include <string.h>

#include "esp_log.h"

#include "driver/i2c.h"

#include "i2cInterface.h"
#include "SFH7776.h"

static uint8_t i2c_addr = 0x39;

#define SFH7776_INTERRUPT_PIN (GPIO_NUM_22)

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

esp_err_t SFH7776_set_lsb_then_msb(uint8_t lsb_addr, uint16_t value){

	uint8_t data[3];
	data[0] = lsb_addr;
	memcpy(data+1, &value, sizeof(uint8_t) *2);
	return i2c_master_write_slave(i2c_addr, data, 3);
}

esp_err_t SFH7776_get_lsb_then_msb(uint8_t lsb_addr, uint16_t * value){
	esp_err_t err = i2c_master_write_slave(i2c_addr, &lsb_addr, 1);
	if(err != ESP_OK)
		return err;

	uint8_t buffer[2] = {0};
	err = i2c_master_read_slave(i2c_addr, buffer, 2);
	*value = (uint16_t)buffer[1] << 8 | buffer[0];
	return err;
}

esp_err_t SFH7776_detect(){

	uint8_t ctrl = 0;
	if(SFH7776_get_system_control(&ctrl) != ESP_OK)
		return ESP_FAIL;

	if((ctrl & 0x3F) != 0x09)
		return ESP_FAIL;

	return ESP_OK;
}

esp_err_t SFH7776_configure_interrupt_pin(bool on, gpio_isr_t handle){

	gpio_config_t pin_config = {
		.pin_bit_mask = 1ULL<<SFH7776_INTERRUPT_PIN,
		.mode = GPIO_MODE_INPUT,
		.pull_up_en = GPIO_PULLUP_ENABLE,
		.pull_down_en = GPIO_PULLDOWN_ENABLE,
		.intr_type = on ? GPIO_INTR_ANYEDGE : GPIO_INTR_DISABLE
	};

	if(gpio_config(&pin_config) != ESP_OK)
		return ESP_FAIL;

	return gpio_isr_handler_add(SFH7776_INTERRUPT_PIN, handle, NULL);
}
