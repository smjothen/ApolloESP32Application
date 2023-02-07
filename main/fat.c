
/* Wear levelling and FAT filesystem example.
   This example code is in the Public Domain (or CC0 licensed, at your option.)
   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
   This sample shows how to store files inside a FAT filesystem.
   FAT filesystem is stored in a partition inside SPI flash, using the
   flash wear levelling library.
*/

#include "fat.h"
#include "errno.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "esp_system.h"
#include "freertos/task.h"
#include "DeviceInfo.h"
#include "zaptec_cloud_observations.h"

static const char *TAG = "FAT            ";

// Handle of the wear levelling library instance
static wl_handle_t s_wl_handle = WL_INVALID_HANDLE;

// Mount path for the partition
const char *base_path = "/spiflash";
/* For testing
void fat_make()
{
    ESP_LOGI(TAG, "Mounting FAT filesystem");
    // To mount device we need name of device partition, define base_path
    // and allow format partition in case if it is new one and was not formated before
    const esp_vfs_fat_mount_config_t mount_config = {
            .max_files = 4,
            .format_if_mount_failed = true,
            .allocation_unit_size = CONFIG_WL_SECTOR_SIZE
    };

    int count = 0;
    while(true)
    {
		esp_err_t err = esp_vfs_fat_spiflash_mount(base_path, "stat", &mount_config, &s_wl_handle);
		if (err != ESP_OK) {
			ESP_LOGE(TAG, "Failed to mount FATFS (%s)", esp_err_to_name(err));
			//return;
		}
		else
			break;

		count++;
    	vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    ESP_LOGI(TAG, "Opening file");
    FILE *f = fopen("/spiflash/hello.txt", "wb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing");
        return;
    }
    fprintf(f, "written using ESP-IDF %s\n", esp_get_idf_version());
    fclose(f);
    ESP_LOGI(TAG, "File written");

    // Open file for reading
    ESP_LOGI(TAG, "Reading file");
    f = fopen("/spiflash/hello.txt", "rb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for reading");
        return;
    }
    char line[128];
    fgets(line, sizeof(line), f);
    fclose(f);
    // strip newline
    char *pos = strchr(line, '\n');
    if (pos) {
        *pos = '\0';
    }
    ESP_LOGI(TAG, "Read from file: '%s'", line);

    // Unmount FATFS
    ESP_LOGI(TAG, "Unmounting FAT filesystem");
    ESP_ERROR_CHECK( esp_vfs_fat_spiflash_unmount(base_path, s_wl_handle));

    ESP_LOGI(TAG, "Done");
}*/



static bool mounted = false;
bool fat_static_mount()
{
	if(mounted)
	{
		ESP_LOGI(TAG, "FAT filesystem is already mounted");
		return mounted;
	}

    ESP_LOGI(TAG, "Mounting FAT filesystem");
    // To mount device we need name of device partition, define base_path
    // and allow format partition in case if it is new one and was not formated before
    const esp_vfs_fat_mount_config_t mount_config = {
            .max_files = 4,
            .format_if_mount_failed = true,
            .allocation_unit_size = CONFIG_WL_SECTOR_SIZE
    };

	esp_err_t err = esp_vfs_fat_spiflash_mount(base_path, "disk", &mount_config, &s_wl_handle);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "Failed to mount FATFS (%s)", esp_err_to_name(err));
		return mounted;
	}

	mounted = true;

	ESP_LOGI(TAG, "Mounted");

	return mounted;
}

bool fatIsMounted()
{
	return mounted;
}


void fat_WriteCertificateBundle(char * newCertificateBundle)
{
	if(mounted == false)
	{
		ESP_LOGE(TAG, "Partition not mounted for writing");
		return;
	}


    ESP_LOGI(TAG, "Opening file");
    FILE *f = fopen("/spiflash/cert.txt", "wb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing");
        return;
    }

    int wrt = fprintf(f, "%s\n", newCertificateBundle);

    fclose(f);
    ESP_LOGI(TAG, "File written: %d bytes", wrt);

    publish_debug_telemetry_security_log("Certificate", "Renewed");

}

void fat_ReadCertificateBundle(char * readCertificateBundle)
{

	if(mounted == false)
	{
		ESP_LOGE(TAG, "Partition not mounted for reading");
		return;
	}

    // Open file for reading
    ESP_LOGI(TAG, "Reading file");
    FILE *f = fopen("/spiflash/cert.txt", "rb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for reading");
        return;
    }

    fgets(readCertificateBundle, MAX_CERTIFICATE_BUNDLE_SIZE, f);
    fclose(f);

    //ESP_LOGW(TAG, "Read cert: %d: %s ", strlen(readCertificateBundle), readCertificateBundle);
}

void fat_DeleteCertificateBundle()
{

	if(mounted == false)
	{
		ESP_LOGE(TAG, "Partition not mounted for reading");
		return;
	}

    // Open file for reading
    ESP_LOGI(TAG, "Reading file");
    int ret = remove("/spiflash/cert.txt");

    ESP_LOGI(TAG, "Removed cert file returned: %d", ret);
}


void fat_static_unmount()
{
	// Unmount FATFS
	ESP_LOGI(TAG, "Unmounting FAT filesystem");
	esp_vfs_fat_spiflash_unmount(base_path, s_wl_handle);

	mounted = false;
	ESP_LOGI(TAG, "Done unmounting");
}

static char fatDiagnostics[150] = {0};

char * fat_GetDiagnostics()
{
	return fatDiagnostics;
}

bool fat_eraseAndRemountPartition()
{
	esp_err_t err = ESP_FAIL;

	const esp_partition_t *part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, "disk");

	if(part != NULL)
	{
		fat_static_unmount();

		err = esp_partition_erase_range(part, 0, part->size);
	}

	mounted = false;

	fat_static_mount();

	snprintf(fatDiagnostics + strlen(fatDiagnostics), sizeof(fatDiagnostics), " Disk erase err: %i ,M: %i", err, mounted);

	return mounted;
}


bool fat_CheckFilesSystem()
{
	bool deletedOK = false;
	bool createdOK = fat_Factorytest_CreateFile();
	if(createdOK)
		deletedOK = fat_Factorytest_DeleteFile();

	snprintf(fatDiagnostics + strlen(fatDiagnostics), sizeof(fatDiagnostics), " Disk file: created = %i, deleted = %i,", createdOK, deletedOK);

	return deletedOK; //True if both bools are OK
}

bool fat_CorrectFilesystem()
{
	return fat_eraseAndRemountPartition();
}


static FILE *testDiskFile = NULL;
bool fat_Factorytest_CreateFile()
{
	if(!fat_static_mount()){
		ESP_LOGE(TAG, "failed to mount disk");
		return false;
	}

	testDiskFile = fopen("/spiflash/testdisk.bin", "wb+");

	ESP_LOGW(TAG, "Create file errno: %i: %s", errno, strerror(errno));

	if(testDiskFile == NULL)
	{
		snprintf(fatDiagnostics + strlen(fatDiagnostics), sizeof(fatDiagnostics), " Disk file = NULL, %i:%s,", errno, strerror(errno));
		return false;
	}
	else
	{
		snprintf(fatDiagnostics + strlen(fatDiagnostics), sizeof(fatDiagnostics), " Disk file = 0x%08x", (unsigned int)testDiskFile);
		fclose(testDiskFile);
	}

	return true;
}

bool fat_Factorytest_DeleteFile()
{
	if(!fat_static_mount()){
		ESP_LOGE(TAG, "failed to mount disk");
		return false;
	}

	int status = remove("/spiflash/testdisk.bin");
	ESP_LOGW(TAG, "Status from remove: %i", status);

	if(status == 0)
	{
		return true;
	}

	return false;
}
