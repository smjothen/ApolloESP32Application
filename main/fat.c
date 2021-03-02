
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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "esp_system.h"
#include "freertos/task.h"

static const char *TAG = "FAT  ";

// Handle of the wear levelling library instance
static wl_handle_t s_wl_handle = WL_INVALID_HANDLE;

// Mount path for the partition
const char *base_path = "/spiflash";

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
}


////

bool mounted = false;
void fat_static_mount()
{
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
		return;
	}

	mounted = true;

	ESP_LOGI(TAG, "Mounted");
}



void fat_WriteCertificate(char * newCertificateBundle)
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

    /*int count = 0;
    int written = 1;
    int sum = 0;
    //while (written > 0)
    //{
    	written = fwrite(newCertificateBundle, 1, strlen(newCertificateBundle), f);
    	sum += written;
    	//count+=1000;
    	ESP_LOGW(TAG, "Count: %d, Written: %d bytes, Sum: %d", count, written, sum);
    	*/
    	//vTaskDelay(10 / portTICK_PERIOD_MS);
    //}
    int wrt = fprintf(f, "%s\n", newCertificateBundle);

    fclose(f);
    ESP_LOGI(TAG, "File written: %d bytes", wrt);

}

void fat_ReadCertificate(char * readCertificateBundle)
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
    //char line[50000];
    fgets(readCertificateBundle, 50000, f);
    fclose(f);
    // strip newline
    /*char *pos = strchr(readCertificateBundle, '\n');
    if (pos) {
        *pos = '\0';
    }*/
    //ESP_LOGI(TAG, "Read from file: '%s'", readCertificateBundle);

}

void fat_static_unmount()
{
	// Unmount FATFS
	ESP_LOGI(TAG, "Unmounting FAT filesystem");
	ESP_ERROR_CHECK( esp_vfs_fat_spiflash_unmount(base_path, s_wl_handle));

	mounted = false;
	ESP_LOGI(TAG, "Done");
}

