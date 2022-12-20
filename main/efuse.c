#include "esp_efuse.h"
#include "esp_efuse_table.h"

#include "DeviceInfo.h"

esp_err_t GetEfuseInfo(struct EfuseInfo * info){

	if(esp_efuse_read_field_blob(ESP_EFUSE_FLASH_CRYPT_CNT, &info->flash_crypt_cnt, 7) != ESP_OK)
		return ESP_FAIL;

	info->write_disabled_flash_crypt_cnt =  esp_efuse_read_field_bit(ESP_EFUSE_WR_DIS_FLASH_CRYPT_CNT);

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

	info->block1_read_disabled = esp_efuse_read_field_bit(ESP_EFUSE_RD_DIS_BLK1);
	info->block1_write_disabled = esp_efuse_read_field_bit(ESP_EFUSE_WR_DIS_BLK1);
	esp_efuse_read_block(EFUSE_BLK1, info->block1, 0, 256);

	info->block2_read_disabled = esp_efuse_read_field_bit(ESP_EFUSE_RD_DIS_BLK2);
	info->block2_write_disabled = esp_efuse_read_field_bit(ESP_EFUSE_WR_DIS_BLK2);
	esp_efuse_read_block(EFUSE_BLK2, info->block2, 0, 256);

	info->block3_read_disabled = esp_efuse_read_field_bit(ESP_EFUSE_RD_DIS_BLK3);
	info->block3_write_disabled = esp_efuse_read_field_bit(ESP_EFUSE_WR_DIS_BLK3);
	esp_efuse_read_block(EFUSE_BLK3, info->block3, 0, 256);

	return ESP_OK;
}
