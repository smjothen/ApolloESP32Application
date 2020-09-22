#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/ledc.h"

//#include "audioBuzzer.h"


//AUDIO
#define LEDC_TEST_CH_NUM_E 0
#define GPIO_OUTPUT_AUDIO   (2)

//static const char *TAG = "AUDIO     ";

static ledc_channel_config_t ledc_channel;

void audioInit()
{
	/*
	 * Prepare and set configuration of timers
	 * that will be used by LED Controller
	 */
	ledc_timer_config_t ledc_timer = {
		.duty_resolution = LEDC_TIMER_13_BIT, // resolution of PWM duty
		.freq_hz = 1000,                      // frequency of PWM signal
		.speed_mode = LEDC_HIGH_SPEED_MODE,           // timer mode
		.timer_num = LEDC_TIMER_0,            // timer index
		.clk_cfg = LEDC_AUTO_CLK,              // Auto select the source clock
	};
	// Set configuration of timer0 for high speed channels
	ledc_timer_config(&ledc_timer);

	/*
		 * Prepare individual configuration
		 * for each channel of LED Controller
		 * by selecting:
		 * - controller's channel number
		 * - output duty cycle, set initially to 0
		 * - GPIO number where LED is connected to
		 * - speed mode, either high or low
		 * - timer servicing selected channel
		 *   Note: if different channels use one timer,
		 *         then frequency and bit_num of these channels
		 *         will be the same
		 */


		ledc_channel_config_t ledc_channel_init = {

			.gpio_num   = 2,
			.speed_mode = LEDC_HIGH_SPEED_MODE,
			.channel    = LEDC_CHANNEL_0,
			.duty       = 0,
			.hpoint     = 0,
			.timer_sel  = LEDC_TIMER_0

		};

		ledc_channel = ledc_channel_init;

		ledc_channel_config(&ledc_channel);


		uint32_t duty = 0;
		ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, duty);
		ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0);

}


void audio_play_nfc_card_accepted()
{
	uint32_t duty = 4000;
	ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, duty);
	ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0);

	ledc_set_freq(LEDC_HIGH_SPEED_MODE, LEDC_TIMER_0, 500);
	vTaskDelay(150 / portTICK_PERIOD_MS);

	ledc_set_freq(LEDC_HIGH_SPEED_MODE, LEDC_TIMER_0, 1000);
	vTaskDelay(100 / portTICK_PERIOD_MS);

	ledc_set_freq(LEDC_HIGH_SPEED_MODE, LEDC_TIMER_0, 1500);
	vTaskDelay(200 / portTICK_PERIOD_MS);

	duty = 0;
	ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, duty);
	ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0);
}

void audio_play_nfc_card_accepted_debug()
{
	uint32_t duty = 4000;
	ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, duty);
	ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0);

	ledc_set_freq(LEDC_HIGH_SPEED_MODE, LEDC_TIMER_0, 700);//500
	vTaskDelay(50 / portTICK_PERIOD_MS);

	duty = 0;
	ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, duty);
	ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0);

}



