#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "esp_log.h"
#include "adc_control.h"
//#include "network.h"
//#include "HANadapter.h"
//#include "storage.h"
//#include "https_client.h"

#define DEFAULT_VREF    1100        //Use adc2_vref_to_gpio() to obtain a better estimate
#define NO_OF_SAMPLES   1//10          //Multisampling

static const char *TAG = "ADC     ";

static esp_adc_cal_characteristics_t *adc_chars;
static const adc_channel_t channel6 = ADC_CHANNEL_6;     //HW_ID	//GPIO34 if ADC1, GPIO14 if ADC2
static const adc_channel_t channel3 = ADC_CHANNEL_3;	 //PWR_MEAS	//ADC_CHANNEL_7;     //GPIO34 if ADC1, GPIO14 if ADC2
static const adc_atten_t atten = ADC_ATTEN_DB_11;
static const adc_unit_t unit = ADC_UNIT_1;



//static void check_efuse()
//{
//    //Check TP is burned into eFuse
//    if (esp_adc_cal_check_efuse(ESP_ADC_CAL_VAL_EFUSE_TP) == ESP_OK) {
//    	ESP_LOGI(TAG, "eFuse Two Point: Supported\n");
//    } else {
//    	ESP_LOGI(TAG, "eFuse Two Point: NOT supported\n");
//    }
//
//    //Check Vref is burned into eFuse
//    if (esp_adc_cal_check_efuse(ESP_ADC_CAL_VAL_EFUSE_VREF) == ESP_OK) {
//    	ESP_LOGI(TAG, "eFuse Vref: Supported\n");
//    } else {
//    	ESP_LOGI(TAG, "eFuse Vref: NOT supported\n");
//    }
//}
//
//static void print_char_val_type(esp_adc_cal_value_t val_type)
//{
//    if (val_type == ESP_ADC_CAL_VAL_EFUSE_TP) {
//    	ESP_LOGI(TAG, "Characterized using Two Point Value\n");
//    } else if (val_type == ESP_ADC_CAL_VAL_EFUSE_VREF) {
//    	ESP_LOGI(TAG, "Characterized using eFuse Vref\n");
//    } else {
//    	ESP_LOGI(TAG, "Characterized using Default Vref\n");
//    }
//}


static void check_efuse(void)
{
    //Check TP is burned into eFuse
    if (esp_adc_cal_check_efuse(ESP_ADC_CAL_VAL_EFUSE_TP) == ESP_OK) {
        printf("eFuse Two Point: Supported\n");
    } else {
        printf("eFuse Two Point: NOT supported\n");
    }

    //Check Vref is burned into eFuse
    if (esp_adc_cal_check_efuse(ESP_ADC_CAL_VAL_EFUSE_VREF) == ESP_OK) {
        printf("eFuse Vref: Supported\n");
    } else {
        printf("eFuse Vref: NOT supported\n");
    }
}

static void print_char_val_type(esp_adc_cal_value_t val_type)
{
    if (val_type == ESP_ADC_CAL_VAL_EFUSE_TP) {
        printf("Characterized using Two Point Value\n");
    } else if (val_type == ESP_ADC_CAL_VAL_EFUSE_VREF) {
        printf("Characterized using eFuse Vref\n");
    } else {
        printf("Characterized using Default Vref\n");
    }
}


static void adc_task()
{
	//Check if Two Point or Vref are burned into eFuse
	check_efuse();

	//Configure ADC
	if (unit == ADC_UNIT_1) {
		adc1_config_width(ADC_WIDTH_BIT_12);
		adc1_config_channel_atten(channel6, atten);
		adc1_config_channel_atten(channel3, atten);
	}

	//Characterize ADC
	adc_chars = calloc(1, sizeof(esp_adc_cal_characteristics_t));
	esp_adc_cal_value_t val_type = esp_adc_cal_characterize(unit, atten, ADC_WIDTH_BIT_12, DEFAULT_VREF, adc_chars);
	print_char_val_type(val_type);

	//Continuously sample ADC1
	while (1) {
		uint32_t adc_reading6 = 0;
		uint32_t adc_reading3 = 0;
		//Multisampling
		for (int i = 0; i < NO_OF_SAMPLES; i++) {
			if (unit == ADC_UNIT_1) {
				adc_reading6 += adc1_get_raw((adc1_channel_t)channel6);
				adc_reading3 += adc1_get_raw((adc1_channel_t)channel3);
			} else {
				//int raw;
				//adc2_get_raw((adc2_channel_t)channel, ADC_WIDTH_BIT_12, &raw);
				//adc_reading += raw;
			}
		}
		adc_reading6 /= NO_OF_SAMPLES;
		adc_reading3 /= NO_OF_SAMPLES;

		//Convert adc_reading to voltage in mV
		float voltage6 = esp_adc_cal_raw_to_voltage(adc_reading6, adc_chars) * 0.001;
		float voltage3 = esp_adc_cal_raw_to_voltage(adc_reading3, adc_chars) * 0.001;

		//int8_t percentage0 = (int8_t)(((voltage0 * voltage0)-(1.6 * 1.6)) / 4.6 * 100);
//		if(percentage0>100)
//			percentage0 = 100;
//		else if(percentage0<0)
//			percentage0 = 0;
//
//		HANEnergyLevel = percentage0;
//		hwIdVoltageLevel = voltage6;

		//ESP_LOGI(TAG, "Raw6: %d\tVoltage6: %.2fV \t Raw0: %d\tVoltage0: %.2fV \t %d%%", adc_reading6, voltage6, adc_reading0, voltage0, percentage0);
		ESP_LOGI(TAG, "Raw6: %d\tHW_ID: %.2fV \t Raw0: %d\tPWR_MEAS: %.2fV", adc_reading6, voltage6, adc_reading3, voltage3);
		vTaskDelay(pdMS_TO_TICKS(5000));
	}

}

uint8_t GetHANEnergyLevel()
{
	return HANEnergyLevel;
}

float GetHwIdVoltageLevel()
{
	return hwIdVoltageLevel;
}

void adc_init(){

#ifndef DO_LOG
    esp_log_level_set(TAG, ESP_LOG_INFO);
#endif

	xTaskCreate(adc_task, "adc_task", 4096, NULL, 3, NULL);
	vTaskDelay(1000 / portTICK_PERIOD_MS);
}
