
#include "freertos/FreeRTOS.h"
//#include "freertos/task.h"
#include "esp_log.h"

#include "esp_console.h"
#include "esp_vfs_dev.h"
#include "driver/uart.h"
#include "linenoise/linenoise.h"
#include "argtable3/argtable3.h"

#include "apollo_console.h"
#include "storage.h"
#include "i2cDevices.h"
#include "eeprom_wp.h"
#include "EEPROM.h"
#include "zaptec_cloud_listener.h"
#include "protocol_task.h"
#include "offline_log.h"

#include "calibration.h"

static const char *TAG = "CONSOLE";
static const char *REPLY_TAG = ">>>>>>";

static bool consoleInUse = false;
static int console_in_use(int argc, char **argv){
	consoleInUse = true;
    ESP_LOGD(REPLY_TAG, "Keeping console alive");
    return 0;
}

static int register_console_in_use(void){
    const esp_console_cmd_t cmd = {
        .command = "active",
        .help = "Keep console alive",
        .hint = NULL,
        .func = &console_in_use,
        .argtable = NULL
    };

    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
    return 0;
}

static int prodtest_read_cmd(int argc, char **argv){
    struct DeviceInfo device_info = i2cReadDeviceInfoFromEEPROM();
    ESP_LOGD(REPLY_TAG, "Current prodtest stage: %d (%d)", device_info.factory_stage, device_info.EEPROMFormatVersion);
    return 0;
}

static int register_prodtest_read_cmd(void){
    const esp_console_cmd_t cmd = {
        .command = "prodtest_get",
        .help = "Print the prodtest stage in eeprom",
        .hint = NULL,
        .func = &prodtest_read_cmd,
        .argtable = NULL
    };

    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
    return 0;
}


static int reboot_cmd(int argc, char **argv){
    esp_restart();
    return 0;
}

static int register_reboot_cmd(void){
    const esp_console_cmd_t cmd = {
        .command = "reboot",
        .help = "Reboot the ESP32",
        .hint = NULL,
        .func = &reboot_cmd,
        .argtable = NULL
    };

    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
    return 0;
}

static int new_id_cmd(int argc, char **argv){
   eeprom_wp_disable_nfc_disable();
	if(EEPROM_WriteFormatVersion(0xFF)!=ESP_OK){
		ESP_LOGE(REPLY_TAG, "Failed to write eeprom");
        return -1;
	}
	eeprom_wp_enable_nfc_enable();
    esp_restart();
    return 0;
}

static int register_new_id_cmd(void){
    const esp_console_cmd_t cmd = {
        .command = "clear_id",
        .help = "Reset ZAPno, and reset",
        .hint = NULL,
        .func = &new_id_cmd,
        .argtable = NULL
    };

    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
    return 0;
}

static int clear_energy_cmd(int argc, char **argv){
    storage_clear_accumulated_energy();
    return 0;
}

static int register_clear_energy_cmd(void){
    const esp_console_cmd_t cmd = {
        .command = "energy_clear",
        .help = "cleat accumulated energy",
        .hint = NULL,
        .func = &clear_energy_cmd,
        .argtable = NULL
    };

    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
    return 0;
}

static struct {
    struct arg_int *stage;
    struct arg_end *end;
} prodtest_write_args;

static int prodtest_write_cmd(int argc, char **argv){
    int nerrors = arg_parse(argc, argv, (void **) &prodtest_write_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, prodtest_write_args.end, argv[0]);
        return 1;
    }

    int stage = prodtest_write_args.stage->ival[0];
    ESP_LOGI(REPLY_TAG, "Writing %d to EEPROM factory stage", stage);

    eeprom_wp_disable_nfc_disable();
	if(EEPROM_WriteFactoryStage(stage)!=ESP_OK){
		ESP_LOGE(REPLY_TAG, "Failed to write eeprom");
        return -1;
	}
	eeprom_wp_enable_nfc_enable();
    ESP_LOGI(REPLY_TAG, "EEPROM written & confirmed");

    return 0;

}

int register_prodtest_write_cmd(void){
    prodtest_write_args.stage = arg_int0(NULL, "stage", "<t>", "Prodtest stage enum");
    prodtest_write_args.end = arg_end(2);

    const esp_console_cmd_t join_cmd = {
        .command = "prodtest_set",
        .help = "Write the prodtest stage to EEPROM",
        .hint = NULL,
        .func = &prodtest_write_cmd,
        .argtable = &prodtest_write_args
    };

    ESP_ERROR_CHECK( esp_console_cmd_register(&join_cmd) );
    return 0;
}

static struct {
    struct arg_int *state;
    struct arg_end *end;
} sim_charge_op_args;

static int sim_charge_op_cmd(int argc, char **argv){
    int nerrors = arg_parse(argc, argv, (void **) &sim_charge_op_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, sim_charge_op_args.end, argv[0]);
        return 1;
    }

    int state = sim_charge_op_args.state->ival[0];
    ESP_LOGI(REPLY_TAG, "Setting charge operation mode to %d", state);

	mcu_simulate_charge_op_mode(state);
	return 0;
}

static int register_sim_charge_op_state(void){
	sim_charge_op_args.state = arg_int0(NULL, NULL, "opmode", "Set simulated charge operation mode");
	sim_charge_op_args.end = arg_end(2);

    const esp_console_cmd_t cmd = {
        .command = "chop",
        .help = "simulate charge operation state (-1=disable, 0=uninit, 1=discon, 2=req, 3=charge, ...)",
        .hint = NULL,
        .func = sim_charge_op_cmd,
        .argtable = &sim_charge_op_args
    };

    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
    return 0;
}

#ifdef CONFIG_HEAP_TRACING_STANDALONE

#include "esp_heap_trace.h"

#define NUM_RECORDS 256

static heap_trace_record_t trace_record[NUM_RECORDS];

static int heap_toggle_cmd(int argc, char **argv) {
    static bool heap_inited = false;
    static bool heap_start = true;

    if (!heap_inited) {
        ESP_LOGI(TAG, "Initializing heap records...");

        ESP_ERROR_CHECK(heap_trace_init_standalone(trace_record, NUM_RECORDS));
        heap_inited = true;
    }


    if (heap_start) {
        ESP_LOGI(TAG, "Starting heap trace...");
        ESP_ERROR_CHECK(heap_trace_start(HEAP_TRACE_LEAKS));

        heap_start = false;
    } else {
        ESP_LOGI(TAG, "Stopping heap trace...");
        ESP_ERROR_CHECK(heap_trace_stop());
        heap_trace_dump();

        heap_start = true;
    }

    return 0;
}

static int register_heap_toggle_cmd(void){
    const esp_console_cmd_t cmd = {
        .command = "heap",
        .help = "toggle heap leak debugging",
        .hint = NULL,
        .func = heap_toggle_cmd,
        .argtable = NULL
    };

    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
    return 0;
}

#endif

//based on https://github.com/espressif/esp-idf/blob/6e776946d01ec0d081d09000c36d23ec1d318c06/examples/system/console/main/console_example_main.c
static void initialize_console(void)
{
    /* Drain stdout before reconfiguring it */
    fflush(stdout);
    fsync(fileno(stdout));

    /* Disable buffering on stdin */
    setvbuf(stdin, NULL, _IONBF, 0);

    /* Minicom, screen, idf_monitor send CR when ENTER key is pressed */
    esp_vfs_dev_uart_port_set_rx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, ESP_LINE_ENDINGS_CR);

    /* Move the caret to the beginning of the next line on '\n' */
    esp_vfs_dev_uart_port_set_tx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, ESP_LINE_ENDINGS_CRLF);

    /* Configure UART. Note that REF_TICK is used so that the baud rate remains
     * correct while APB frequency is changing in light sleep mode.
     */
    const uart_config_t uart_config = {
            .baud_rate = CONFIG_ESP_CONSOLE_UART_BAUDRATE,
            .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .source_clk = UART_SCLK_REF_TICK,
    };
    /* Install UART driver for interrupt-driven reads and writes */
    ESP_ERROR_CHECK( uart_driver_install(CONFIG_ESP_CONSOLE_UART_NUM,
            256, 0, 0, NULL, 0) );
    ESP_ERROR_CHECK( uart_param_config(CONFIG_ESP_CONSOLE_UART_NUM, &uart_config) );

    /* Tell VFS to use UART driver */
    esp_vfs_dev_uart_use_driver(CONFIG_ESP_CONSOLE_UART_NUM);

    /* Initialize the console */
    esp_console_config_t console_config = {
            .max_cmdline_args = 8,
            .max_cmdline_length = 256,
#ifdef CONFIG_LOG_COLORS
            .hint_color = atoi(LOG_COLOR_CYAN)
#else
	    .hint_color = 36
#endif /* CONFIG_LOG_COLORS */
    };
    ESP_ERROR_CHECK( esp_console_init(&console_config) );

    /* Configure linenoise line completion library */
    /* Enable multiline editing. If not set, long commands will scroll within
     * single line.
     */
    linenoiseSetMultiLine(1);

    /* Tell linenoise where to get command completions and hints */
    linenoiseSetCompletionCallback(&esp_console_get_completion);
    linenoiseSetHintsCallback((linenoiseHintsCallback*) &esp_console_get_hint);

    /* Set command history size */
    linenoiseHistorySetMaxLen(10);

    /* Don't return empty lines */
    //linenoiseAllowEmpty(false);
}


static bool run = true;
void console_task(){
    const char *prompt =  "APOLLO ESP> ";

    while(run) {
            /* Get a line using linenoise.
            * The line is returned when ENTER is pressed.
            */
            char* line = linenoise(prompt);
            if (line == NULL) { /* ignore EOF or error */
                continue;
            }
            /* Add the command to the history if not empty*/
            if (strlen(line) > 0) {
                linenoiseHistoryAdd(line);
            }

            /* Try to run the command */
            int ret;
            esp_err_t err = esp_console_run(line, &ret);
            if (err == ESP_ERR_NOT_FOUND) {
                printf("Unrecognized command\n");
            } else if (err == ESP_ERR_INVALID_ARG) {
                // command was empty
            } else if (err == ESP_OK && ret != ESP_OK) {
                printf("Command returned non-zero error code: 0x%x (%s)\n", ret, esp_err_to_name(ret));
            } else if (err != ESP_OK) {
                printf("Internal error: %s\n", esp_err_to_name(err));
            }
            /* linenoise allocates line buffer on the heap, so need to free it */
            linenoiseFree(line);
    }

    ESP_LOGE(TAG, "Error or end-of-input, terminating console");
    esp_console_deinit();
}

static TaskHandle_t console_task_handle = NULL;

void apollo_console_init(void){
    ESP_LOGI(TAG, "Configuring esp_console");

    initialize_console();
    esp_console_register_help_command();

    register_console_in_use();
    register_prodtest_read_cmd();
    register_prodtest_write_cmd();
    register_reboot_cmd();
    register_new_id_cmd();
    register_clear_energy_cmd();
	register_sim_charge_op_state();

    xTaskCreate(console_task, "console_task", 4096, NULL, 2, &console_task_handle);
}

void console_stop()
{
	if(consoleInUse == false)
	{
		run = false;
		//deInit does not work, never return or crashes when ending thread, suspending thread instead.
		//esp_console_deinit();
		uart_disable_rx_intr(CONFIG_ESP_CONSOLE_UART_NUM);
		vTaskSuspend(console_task_handle);
		ESP_LOGI(TAG, "Console suspended");
	}
	else
	{
		ESP_LOGI(TAG, "Keeping console open");
	}
}
