#include "esp_log.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "pic_update.h"

#include "zaptec_protocol_serialisation.h"
#include "protocol_task.h"
#include "mcu_communication.h"
#include "crc32.h"

#define TAG "pic_update"

// Commands accepted by Pro bootloader
#define COMMAND_PRO_ERASE_APP   0x01
#define COMMAND_PRO_WRITE_PM    0x02
#define COMMAND_PRO_START_APP   0x03
#define COMMAND_PRO_VERSION     0x04
#define COMMAND_PRO_LED_BLUE    0x05

// Debug commands
#define COMMAND_PRO_READ_PM     0x06
#define COMMAND_PRO_RUN_ID      0x07

#define COMMAND_PRO_EVENT_LOG_ERROR 0x08

#define MODE_APP_WITH_BOOTLOADER 3
#define MODE_APP_ONLY 4

// ZEncodeMessageHeader* does not check the length of the buffer!
// This should not be a problem for most usages, but make sure strings are within a range that fits!
//
// Go+ bootloader sends whole pages so must be large enough to fit a page!
#define ZAP_PLUS_PROTOCOL_BUFFER_SIZE 2100
#define ZAP_PLUS_PROTOCOL_BUFFER_SIZE_ENCODED (ZAP_PROTOCOL_BUFFER_SIZE + 1 /* overhead byte */ + 1 /* delimiter byte */ + 0 /* ~one byte overhead per 256 bytes */ + 5 /*extra*/)

#define PIC24_PAGE_SIZE (3 + 2 + 1536) // 3 addr bytes, 2 padding bytes, then 1536 bytes data

extern const uint8_t _pic_bin_start[] asm("_binary_pic_bin_start");
extern const uint8_t _pic_bin_end[] asm("_binary_pic_bin_end");

static const char *image_version = NULL;
static const uint8_t *image_start = NULL;
static const uint8_t *image_end = NULL;

extern uint8_t bootloaderVersion;

static const int DSPIC_UPDATE_TIMEOUT_MS = 1000*50;
static EventGroupHandle_t event_group;
static const int DSPIC_COMMS_ERROR = BIT0;
static const int DSPIC_UPDATE_COMPLETE = BIT1;

static ZapMessage txMsg;

static char versionBuf[32];

static uint8_t txBuf[ZAP_PLUS_PROTOCOL_BUFFER_SIZE];
static uint8_t encodedTxBuf[ZAP_PLUS_PROTOCOL_BUFFER_SIZE_ENCODED];

static int is_bootloader(bool *result);
static int enter_bootloader(void);
static int get_application_mode(void);
static int transfer_pic_fw(void);
static int read_fw_version(void);
static int boot_pic_app(void);
static int delete_pic_fw(void);

bool is_goplus(void) {
    // Hardcode for now!
    return true;
    /*
    bool detected;
    if (is_bootloader(&detected) || get_application_mode()) {
        return true;
    }
    return false;
    */
}

static void update_pic_task(void *pvParameters) {
    bool bootloader_detected = false;

    // Go bootloader does not support FirmwareAck with length, it is always 1, so
    // turn this on when talking to Go+ MCU
    ZEncodeFirmwareAckHasLength(true);

    if (get_application_mode() == MODE_APP_ONLY) {
        ESP_LOGI(TAG, "app is without bootloader");
        bootloaderVersion = 0x86;
        goto success;
    }

    if(is_bootloader(&bootloader_detected)==0){
        ESP_LOGI(TAG, "already in bootloader, trying to start app!");
        // First if in bootloader, try starting app

        if (boot_pic_app() < 0) {
            // Fail to boot,
            ESP_LOGI(TAG, "booting failed, trying to program!");
            goto program;
        }
    }

    vTaskDelay(pdMS_TO_TICKS(1000));

    // Check if Go+ application is running, if so, enter bootloader
    if (get_application_mode() == MODE_APP_WITH_BOOTLOADER) {
        if (read_fw_version() < 0) {
            goto err_fw_version;
        }

        if (strcmp(versionBuf, image_version) == 0) {
            ESP_LOGI(TAG, "application same as embedded image: %s", image_version);
            goto success;
        } else {
            ESP_LOGI(TAG, "application different, updating: %s -> %s!", versionBuf, image_version);
        }

        int bootloader_enter_result = enter_bootloader();
        if(bootloader_enter_result<0){
            ESP_LOGW(TAG, "failure in enter bootloader comand");
            goto err_bootloader_enter;
        }

        vTaskDelay(pdMS_TO_TICKS(1000));

        if(is_bootloader(&bootloader_detected)<0){
            ESP_LOGW(TAG, "failed to check for bootloader after command");
            if(!bootloader_detected){
                ESP_LOGW(TAG, "could not enter bootloader correctly");
                goto err_bootloader_enter;
            }
        }
    }

    // Confirm still in bootloader, or no application programmed yet
    if(is_bootloader(&bootloader_detected)<0){
        ESP_LOGW(TAG, "failed to check for bootloader after command");
        if(!bootloader_detected){
            ESP_LOGW(TAG, "could not enter bootloader correctly");
            goto err_bootloader_enter;
        }
    }

program:

    if(delete_pic_fw()>=0){
        ESP_LOGI(TAG, "update stage delete: success!");
    }else{
        goto err_delete;
    }

    if(transfer_pic_fw()>=0){
        ESP_LOGI(TAG, "update stage transfer: success!");
    }else{
        goto err_flash;
    }

    if(boot_pic_app()>=0){
        ESP_LOGI(TAG, "update stage boot: success!");
    }else{
        goto err_app_boot;
    }

    ESP_LOGI(TAG, "SUCCESS, pic updated");

success:
    xEventGroupSetBits(event_group, DSPIC_UPDATE_COMPLETE);
    vTaskSuspend(NULL); // halts the task, ensuring we dont run the error handeling

err_fw_version:
    ESP_LOGW(TAG, "pic failed to read fw version");
err_bootloader_enter:
    ESP_LOGW(TAG, "pic failed to enter bootloader");
err_delete:
    ESP_LOGW(TAG, "failed to delete pic fw");
err_flash:
    ESP_LOGW(TAG, "failed to flash pic fw");
err_app_boot:
    ESP_LOGW(TAG, "failed to boot new fw");

    xEventGroupSetBits(event_group, DSPIC_COMMS_ERROR);
    vTaskSuspend(NULL);
}

static int validate_dspic_reply(ZapMessage rxMsg, int type, int length, uint8_t error_code){
    if(rxMsg.type != type){
        ESP_LOGW(TAG, "unexpected rx message type (%d)", rxMsg.type);
        return -1;
    }

    if(length > 0 && rxMsg.length != length){
        ESP_LOGW(TAG, "unexpected rx message length (%d)", rxMsg.length);
        return -2;
    }

    if(rxMsg.data[0] != error_code){
        ESP_LOGW(TAG, "unexpected error code in rx message (error: %d)", rxMsg.data[0]);
        return -3;
    }

    return 0;
}

static int is_bootloader(bool *result){
    ESP_LOGI(TAG, "reading bootloader version");

    int err = 0;

    txMsg.type = MsgFirmware;
    txMsg.identifier = COMMAND_PRO_VERSION;

    uint encoded_length = ZEncodeMessageHeaderAndOneByte(
        &txMsg, 0xff, txBuf, encodedTxBuf
    );

    ZapMessage rxMsg = runRequest(encodedTxBuf, encoded_length);
    if(validate_dspic_reply(rxMsg, MsgFirmwareAck, 1, 0x86)==0){
        bootloaderVersion = rxMsg.data[0];
    	ESP_LOGI(TAG, "detected bootloader version: %d", bootloaderVersion);
        *result = true;
    } else {
        ESP_LOGE(TAG, "inconclusive bootloader test, assuming false");
        err = -1;
        *result = false;
    }

    freeZapMessageReply();
    return err;
}

static int read_fw_version(void) {
    ESP_LOGI(TAG, "reading current fw version");

    txMsg.type = MsgRead;
    txMsg.identifier = ParamSmartMainboardAppSwVersion;

    uint encoded_length = ZEncodeMessageHeaderOnly(
        &txMsg, txBuf, encodedTxBuf
    );

    ZapMessage rxMsg = runRequest(encodedTxBuf, encoded_length);

    if(rxMsg.type != MsgReadAck || rxMsg.length <= 0){
        ESP_LOGW(TAG, "error in dspic response when reading FW version");
        freeZapMessageReply();
        return -1;
    }

    if (rxMsg.length >= sizeof (versionBuf)) {
        ESP_LOGW(TAG, "too long version code");
        return -1;
    }

    strncpy(versionBuf, (const char *)rxMsg.data, rxMsg.length);

    freeZapMessageReply();
    return 0;
}


#define CMD_ACK_SUCCESS 0
#define CMD_ACK_COMPAT_OK 16

static int transfer_pic_fw(void){
    ESP_LOGI(TAG, "sending fw to pic");

    assert(image_version);
    assert(image_start);
    assert(image_end);

    assert((image_end - image_start) % PIC24_PAGE_SIZE == 0);

    unsigned char *entry = (unsigned char *)image_start;

    txMsg.type = MsgFirmware;
    txMsg.identifier = COMMAND_PRO_WRITE_PM;

    while(entry < image_end) {
        uint encoded_length = ZEncodeMessageHeaderAndByteArrayNoCheck(
            &txMsg, (char *) entry, PIC24_PAGE_SIZE, txBuf, encodedTxBuf
        );

        uint32_t addr = (entry[0] << 16) | (entry[1] << 8) | entry[2];

        ESP_LOGI(TAG, "sending fw page %d at addr %" PRIu32 " length %d", (entry - image_start) / PIC24_PAGE_SIZE, addr, encoded_length);

        ZapMessage rxMsg = runRequest(encodedTxBuf, encoded_length);

        int expected = CMD_ACK_SUCCESS;
        if (addr == 0x13000) {
            expected = CMD_ACK_COMPAT_OK;
        }

        if(validate_dspic_reply(rxMsg, MsgFirmwareAck, 1, expected)<0){
            ESP_LOGW(TAG, "error in dspic response when writing FW");
            freeZapMessageReply();
            return -1;
        }

        freeZapMessageReply();
        entry += PIC24_PAGE_SIZE;
    }

    return 0;
}

static int boot_pic_app(void){
    ESP_LOGI(TAG, "starting dsPIC app");

    txMsg.type = MsgFirmware;
    txMsg.identifier = COMMAND_PRO_START_APP;

    uint encoded_length = ZEncodeMessageHeaderOnly(&txMsg, txBuf, encodedTxBuf);
    ZapMessage rxMsg = runRequest(encodedTxBuf, encoded_length);

    if(validate_dspic_reply(rxMsg, MsgFirmwareAck, 1, 0)<0){
        ESP_LOGW(TAG, "error in dspic response when giving boot command");
        freeZapMessageReply();
        return -1;
    }

    freeZapMessageReply();
    return 0;
}

static int delete_pic_fw(void){
    ESP_LOGI(TAG, "deleting PIC FW");

    txMsg.type = MsgFirmware;
    txMsg.identifier = COMMAND_PRO_ERASE_APP;

    uint encoded_length = ZEncodeMessageHeaderOnly(&txMsg, txBuf, encodedTxBuf);
    ZapMessage rxMsg = runRequest(encodedTxBuf, encoded_length);

    if(validate_dspic_reply(rxMsg, MsgFirmwareAck, 1, 0)<0){
        ESP_LOGW(TAG, "error in dspic response when deleteing app");
        freeZapMessageReply();
        return -1;
    }

    freeZapMessageReply();
    return 0;
}

static int get_application_mode(void) {
    ESP_LOGI(TAG, "reading application mode");

    txMsg.type = MsgRead;
    // Pro responds to this but I don't think Go does, so can use this to detect if we're
    // running the Pro MCU application.
    txMsg.identifier = ParamMode;

    uint encoded_length = ZEncodeMessageHeaderOnly(&txMsg, txBuf, encodedTxBuf);
    ZapMessage rxMsg = runRequest(encodedTxBuf, encoded_length);

    freeZapMessageReply();
    return rxMsg.data[0];
}

static int enter_bootloader(void){
    ESP_LOGI(TAG, "sending enter bootloader command");

    txMsg.type = MsgCommand;
    txMsg.identifier = CommandUpgradeMcuFirmware;

    uint encoded_length = ZEncodeMessageHeaderAndOneByte(
        &txMsg, 42, txBuf, encodedTxBuf
    );

    ZapMessage rxMsg = runRequest(encodedTxBuf, encoded_length);

    if(validate_dspic_reply(rxMsg, MsgCommandAck, 1, 0)<0){
        ESP_LOGW(TAG, "failed start bootloader on dspic");
        freeZapMessageReply();
        return -1;
    }

    freeZapMessageReply();
    return 0;
}

int update_goplus(void){
    image_version = (char *)_pic_bin_start;
    image_start = (uint8_t *)strchr((char *)_pic_bin_start, 0) + 1;
    image_end = _pic_bin_end;

    ESP_LOGI(TAG, "Embedded PIC MCU version %s", image_version);

    event_group = xEventGroupCreate();
    bool update_success = false;

    for(int retry=0; retry<10; retry++){

        if(retry == 0){
            ESP_LOGI(TAG, "starting dspic update task");
        }else{
            int delay = 1000 * retry;
            vTaskDelay(pdMS_TO_TICKS(delay));
            ESP_LOGI(TAG, "retrying dspic update task (attempt: %d, delay: %d ms)", retry+1, delay);
        }

        xEventGroupClearBits(event_group,
            DSPIC_COMMS_ERROR | DSPIC_UPDATE_COMPLETE);

        static uint8_t ucParameterToPass = {0};
        TaskHandle_t taskHandle = NULL;
        int stack_size = 4096*2;
        xTaskCreate(
            &update_pic_task, "picflash", stack_size,
            &ucParameterToPass, 4, &taskHandle
        );

        if(taskHandle == NULL){
            ESP_LOGW(TAG, "Failed to start dspic update task");
            continue;
        }

        EventBits_t task_results = xEventGroupWaitBits(event_group,
            DSPIC_COMMS_ERROR | DSPIC_UPDATE_COMPLETE,
            pdFALSE, pdFALSE, pdMS_TO_TICKS(DSPIC_UPDATE_TIMEOUT_MS));

        if(task_results & DSPIC_UPDATE_COMPLETE){
            ESP_LOGI(TAG, "update success, terminating update task (attempt: %d)", retry+1);
            update_success = true;
        }else if (task_results & DSPIC_COMMS_ERROR){
            ESP_LOGW(TAG, "error while updating dspic(attempt: %d)", retry+1);
        }else{
            ESP_LOGW(TAG, "timeout while updating dspic(attempt: %d)", retry+1);
        }

        vTaskDelete(taskHandle);
        // we can not be certain the task released the rx message before it was terminated, lets make sure we dont lock up the system
        freeZapMessageReply();

        ESP_LOGD(TAG, "task handle deleted");

        if(update_success == true){
            break;
        }

    }

    if(update_success == false){
        return -1;
    }

    return 0;
}
