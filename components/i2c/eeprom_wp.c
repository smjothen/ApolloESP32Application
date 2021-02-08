#include "eeprom_wp.h"
#include "driver/gpio.h"

#define GPIO_OUTPUT_EEPROM_WP    4
#define GPIO_OUTPUT_EEPROM_WP_SEL (1ULL<<GPIO_OUTPUT_EEPROM_WP)

void eeprom_wp_pint_init(){
    gpio_config_t output_conf;
	output_conf.intr_type = GPIO_PIN_INTR_DISABLE;
	output_conf.mode = GPIO_MODE_OUTPUT;
	output_conf.pin_bit_mask = GPIO_OUTPUT_EEPROM_WP_SEL;
	output_conf.pull_down_en = 0;
	output_conf.pull_up_en = 0;
	gpio_config(&output_conf);
}
void eeprom_wp_enable_nfc_enable(){
    gpio_set_level(GPIO_OUTPUT_EEPROM_WP, 1);
}


void eeprom_wp_disable_nfc_disable(){
    gpio_set_level(GPIO_OUTPUT_EEPROM_WP, 0);
}