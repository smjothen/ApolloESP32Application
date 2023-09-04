#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#include "adc_control.h"

#define DEFAULT_VREF    1100        //Use adc2_vref_to_gpio() to obtain a better estimate
#define NO_OF_SAMPLES   1//10          //Multisampling

static TaskHandle_t adcTaskHandle = NULL;

static const adc_channel_t channel_hw_id = ADC_CHANNEL_6;     //HW_ID	//GPIO34 if ADC1, GPIO14 if ADC2
static const adc_channel_t channel_pwr_meas = ADC_CHANNEL_3;	 //PWR_MEAS	//ADC_CHANNEL_7;     //GPIO34 if ADC1, GPIO14 if ADC2
static const adc_atten_t atten = ADC_ATTEN_DB_11;
static const adc_unit_t unit = ADC_UNIT_1;

static adc_cali_handle_t adc_cali_handle;
static adc_oneshot_unit_handle_t adc1_handle;
static adc_oneshot_unit_init_cfg_t init_config1 = {
	.unit_id  = unit,
	.ulp_mode = ADC_ULP_MODE_DISABLE,
};
static adc_oneshot_chan_cfg_t config = {
	.bitwidth = ADC_BITWIDTH_12,
	.atten	  = atten,
};
static adc_cali_line_fitting_config_t cali_config = {
	.unit_id  = unit,
	.atten	  = atten,
	.bitwidth = ADC_BITWIDTH_DEFAULT,
};
static adc_cali_line_fitting_efuse_val_t cali_val;

float voltage6HWid = 0.0;
float voltage3PwrMeas = 0.0;

static void check_efuse(void)
{
		ESP_ERROR_CHECK(adc_cali_scheme_line_fitting_check_efuse(&cali_val));

    //Check TP is burned into eFuse
    if (cali_val == ADC_CALI_LINE_FITTING_EFUSE_VAL_EFUSE_TP) {
        printf("eFuse Two Point: Supported\n");
    } else {
        printf("eFuse Two Point: NOT supported\n");
    }

    //Check Vref is burned into eFuse
    if (cali_val == ADC_CALI_LINE_FITTING_EFUSE_VAL_EFUSE_VREF) {
        printf("eFuse Vref: Supported\n");
    } else {
        printf("eFuse Vref: NOT supported\n");
    }
}

static void adc_task()
{
	//Check if Two Point or Vref are burned into eFuse
	check_efuse();

	//Configure ADC
	if (unit == ADC_UNIT_1) {
		ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));
		ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, channel_hw_id, &config));
		ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, channel_pwr_meas, &config));
	}

	ESP_ERROR_CHECK(adc_cali_create_scheme_line_fitting(&cali_config, &adc_cali_handle));

	//Continuously sample ADC1
	while (1) {

		int adc_reading_hw_id = 0;
		int adc_reading_hw_id_sample = 0;
		int adc_reading_pwr_meas = 0;
		int adc_reading_pwr_meas_sample = 0;

		//Multisampling
		for (int i = 0; i < NO_OF_SAMPLES; i++) {
			if (unit == ADC_UNIT_1) {
				ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, channel_hw_id, &adc_reading_hw_id_sample));
				ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, channel_pwr_meas, &adc_reading_pwr_meas_sample));
				adc_reading_hw_id += adc_reading_hw_id_sample;
				adc_reading_pwr_meas += adc_reading_pwr_meas_sample;
			} else {
				//int raw;
				//adc2_get_raw((adc2_channel_t)channel, ADC_WIDTH_BIT_12, &raw);
				//adc_reading += raw;
			}
		}
		adc_reading_hw_id /= NO_OF_SAMPLES;
		adc_reading_pwr_meas /= NO_OF_SAMPLES;


		int voltage_hw_id_int;
		ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc_cali_handle, adc_reading_hw_id, &voltage_hw_id_int));
		voltage6HWid = (float)voltage_hw_id_int / 1000.0;

		int voltage_pwr_meas_int;
		ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc_cali_handle, adc_reading_pwr_meas, &voltage_pwr_meas_int));
		voltage3PwrMeas = (float)voltage_pwr_meas_int / 1000.0;

		//ESP_LOGI(TAG, "Raw6: %d\tHW_ID: %.2fV \t Raw0: %d\tPWR_MEAS: %.2fV", adc_reading6, voltage6HWid, adc_reading3, voltage3PwrMeas);
		vTaskDelay(pdMS_TO_TICKS(500));
	}

}

float GetHardwareId()
{
	return voltage6HWid;
}

float GetPowerMeas()
{
	return voltage3PwrMeas;
}


int adcGetStackWatermark()
{
	if(adcTaskHandle != NULL)
		return uxTaskGetStackHighWaterMark(adcTaskHandle);
	else
		return -1;
}

void adc_init(){

/*#ifndef DO_LOG
    esp_log_level_set(TAG, ESP_LOG_INFO);
#endif*/

    xTaskCreate(adc_task, "adc_task", 2048, NULL, 2, &adcTaskHandle);
	vTaskDelay(1000 / portTICK_PERIOD_MS);
}
