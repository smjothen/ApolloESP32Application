#include "esp_efuse.h"
#include "esp_efuse_table.h"
#include "soc/efuse_reg.h"

#include "DeviceInfo.h"

static const char * TAG = "EFUSE          ";

esp_err_t GetEfuseInfo(struct EfuseInfo * info){

	uint32_t reg =  esp_efuse_read_reg(EFUSE_BLK0, 0);

	info->write_protect = (uint16_t)(reg & 0xffff);
	info->read_protect = (uint8_t)((reg>>16) & 0xf);

	reg = esp_efuse_read_reg(EFUSE_BLK0, 6);
	info->coding_scheme = reg & 0b11;
	info->key_status = reg & 1<<10;

	if(esp_efuse_read_field_blob(ESP_EFUSE_FLASH_CRYPT_CNT, &info->flash_crypt_cnt, 7) != ESP_OK)
		return ESP_FAIL;

	info->disabled_uart_download = esp_efuse_read_field_bit(ESP_EFUSE_UART_DOWNLOAD_DIS);

	if(esp_efuse_read_field_blob(ESP_EFUSE_ENCRYPT_CONFIG, &info->encrypt_config, 4) != ESP_OK)
		return ESP_FAIL;

	info->disabled_console_debug = esp_efuse_read_field_bit(ESP_EFUSE_CONSOLE_DEBUG_DISABLE);

	info->enabled_secure_boot_v1 = esp_efuse_read_field_bit(ESP_EFUSE_ABS_DONE_0);

	static const esp_efuse_desc_t ABS_DONE_1[] = {
		{EFUSE_BLK0, 197, 1},
	};
	const esp_efuse_desc_t* ESP_EFUSE_ABS_DONE_1[] = {
		&ABS_DONE_1[0],
		NULL
	};
	info->enabled_secure_boot_v2 = esp_efuse_read_field_bit(ESP_EFUSE_ABS_DONE_1);

	info->disabled_jtag = esp_efuse_read_field_bit(ESP_EFUSE_DISABLE_JTAG);
	info->disabled_dl_encrypt = esp_efuse_read_field_bit(ESP_EFUSE_DISABLE_DL_ENCRYPT);
	info->disabled_dl_decrypt = esp_efuse_read_field_bit(ESP_EFUSE_DISABLE_DL_DECRYPT);
	info->disabled_dl_cache = esp_efuse_read_field_bit(ESP_EFUSE_DISABLE_DL_CACHE);

	esp_efuse_read_block(EFUSE_BLK1, info->block1, 0, 256);
	esp_efuse_read_block(EFUSE_BLK2, info->block2, 0, 256);
	esp_efuse_read_block(EFUSE_BLK3, info->block3, 0, 256);

	return ESP_OK;
}

esp_err_t lock_encryption_on_if_enabled(){

	size_t cnt;
	esp_err_t err = esp_efuse_read_field_cnt(ESP_EFUSE_FLASH_CRYPT_CNT, &cnt);
	if(err != ESP_OK)
		return err;

	if(cnt % 2 != 1){
		ESP_LOGW(TAG, "Encryption is not enabled");
		return ESP_OK;
	}

	if(cnt == 7){
		ESP_LOGW(TAG, "Encryption already enabled");
		return ESP_OK;
	}

	ESP_LOGI(TAG, "Locking encryption to 'enabled'");
	return esp_efuse_write_field_cnt(ESP_EFUSE_FLASH_CRYPT_CNT, 7-cnt);
}
