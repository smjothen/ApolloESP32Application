
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
#include <dirent.h>
#include <errno.h>
#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "esp_system.h"
#include "freertos/task.h"
#include "DeviceInfo.h"
#include "zaptec_cloud_observations.h"

static const char *TAG = "FAT            ";

// Handle of the wear levelling library instance
static wl_handle_t s_wl_handle = WL_INVALID_HANDLE;
#define PARTITION_COUNT 2
const char * base_paths[PARTITION_COUNT] = {"/disk","/files"}; // partition label with '/' prefix
const int max_files[PARTITION_COUNT] = {256, 256};
bool disable_mount[PARTITION_COUNT] = {false, false};

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


esp_err_t fat_mount(enum fat_id id){

	if(id > PARTITION_COUNT){
		ESP_LOGE(TAG, "Invalid fat_id for mount");
		return ESP_ERR_INVALID_ARG;
	}

	if(disable_mount[id]){
		ESP_LOGW(TAG, "Mounting disabled for requested partition");
		return ESP_ERR_INVALID_STATE;
	}

	ESP_LOGI(TAG, "Mounting %s", base_paths[id]);

	esp_vfs_fat_mount_config_t mount_config = {
		.max_files = max_files[id],
		.format_if_mount_failed = true,
		.allocation_unit_size = CONFIG_WL_SECTOR_SIZE
	};

	return esp_vfs_fat_spiflash_mount_rw_wl(base_paths[id], base_paths[id]+1, &mount_config, &s_wl_handle);
}

void fat_static_mount(void)
{
	ESP_LOGI(TAG, "Mounting FAT partitions");
	ESP_LOGI(TAG, "=======================");

	for(size_t i = 0; i < PARTITION_COUNT; i++){
		ESP_LOGI(TAG, "%s partition:", base_paths[i]+1); // skip path prefix ('/')

		if(stat(base_paths[i], NULL) == 0){
			ESP_LOGW(TAG, "\tAlready mounted");
			break;
		}

		if(errno != ENOENT && errno != ENODEV){
			ESP_LOGE(TAG, "\tUnexpected stat error: %s", strerror(errno));
		}

		esp_err_t err = fat_mount(i);
		if (err != ESP_OK) {
			ESP_LOGE(TAG, "\tMount failed: (%s)", esp_err_to_name(err));
		}else{
			ESP_LOGI(TAG, "\tMount success");
		}
	}
}

void fat_disable_mounting(enum fat_id id, bool disable){
	disable_mount[id] = disable;
}

void fat_static_unmount(void)
{
	ESP_LOGI(TAG, "Unmounting FAT partitions");
	ESP_LOGI(TAG, "=======================");

	for(size_t i = 0; i < PARTITION_COUNT; i++){
		ESP_LOGI(TAG, "%s partition:", base_paths[i]+1);
		if(esp_vfs_fat_spiflash_unmount_rw_wl(base_paths[i], s_wl_handle) != ESP_OK){
			ESP_LOGE(TAG, "\tUnmount failed");
		}else{
			ESP_LOGI(TAG, "\tUnmount success");
		}
	}
}

esp_err_t fat_unmount(enum fat_id id){
	if(id >= PARTITION_COUNT){
		ESP_LOGE(TAG, "Invalid fat_id to unmount");
		return ESP_ERR_INVALID_ARG;
	}

	ESP_LOGI(TAG, "Unmounting %s", base_paths[id]);

	return esp_vfs_fat_spiflash_unmount_rw_wl(base_paths[id], s_wl_handle);
}

bool fatIsMounted(void)
{
	struct stat st;
	return stat("/disk", &st) == 0;
}

void fat_WriteCertificateBundle(char * newCertificateBundle)
{
	if(!fatIsMounted())
	{
		ESP_LOGE(TAG, "Partition not mounted for writing");
		return;
	}

    ESP_LOGI(TAG, "Opening file");
    FILE *f = fopen("/disk/cert.txt", "wb");
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
	if(!fatIsMounted())
	{
		ESP_LOGE(TAG, "Partition not mounted for writing");
		return;
	}

    // Open file for reading
    ESP_LOGI(TAG, "Reading file");
    FILE *f = fopen("/disk/cert.txt", "rb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for reading");
        return;
    }

    fgets(readCertificateBundle, MAX_CERTIFICATE_BUNDLE_SIZE, f);
    fclose(f);

    //ESP_LOGW(TAG, "Read cert: %d: %s ", strlen(readCertificateBundle), readCertificateBundle);
}

void fat_DeleteCertificateBundle(void)
{
	if(!fatIsMounted())
	{
		ESP_LOGE(TAG, "Partition not mounted for writing");
		return;
	}

    // Open file for reading
    ESP_LOGI(TAG, "Reading file");
    int ret = remove("/disk/cert.txt");

    ESP_LOGI(TAG, "Removed cert file returned: %d", ret);
}

#define FAT_DIAG_BUF_SIZE 150
static char fatDiagnostics[FAT_DIAG_BUF_SIZE] = {0};

void fat_ClearDiagnostics(void)
{
	memset(fatDiagnostics, 0, FAT_DIAG_BUF_SIZE);
}

char * fat_GetDiagnostics(void)
{
	return fatDiagnostics;
}

esp_err_t fat_eraseAndRemountPartition(enum fat_id id, char * diagBuf, int diagBufMaxSize, int diagBufUsedLen)
{
	if(id >= PARTITION_COUNT)
		return ESP_ERR_INVALID_ARG;

	esp_err_t err = ESP_FAIL;


	const esp_partition_t *part = NULL;
	if(id == eFAT_ID_DISK)
	{
		part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, "disk");
		ESP_LOGI(TAG, "Erasing disk, part = 0x%X", (unsigned int)part);
	}
	else if(id == eFAT_ID_FILES)
	{
		part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, "files");
		ESP_LOGI(TAG, "Erasing files, part = 0x%X", (unsigned int)part);
	}

	if(part != NULL)
	{
		fat_unmount(id);
		err = esp_partition_erase_range(part, 0, part->size);
	}
	else
	{
		return ESP_ERR_NOT_FOUND;
	}

	fat_mount(id);

	snprintf(diagBuf + diagBufUsedLen, diagBufMaxSize - diagBufUsedLen, " Part %i erase err: %i ,M: %i", id, err, fatIsMounted());

	return err;
}


bool fat_CheckFilesSystem(void)
{
	bool deletedOK = false;
	bool createdOK = fat_Factorytest_CreateFile();
	if(createdOK)
		deletedOK = fat_Factorytest_DeleteFile();

	int fatDiagLen = 0;
	if(fatDiagnostics[0])
		fatDiagLen = strlen(fatDiagnostics);

	snprintf(fatDiagnostics + fatDiagLen, FAT_DIAG_BUF_SIZE - fatDiagLen, " Disk file: created = %i, deleted = %i,", createdOK, deletedOK);

	return deletedOK; //True if both bools are OK
}

bool fat_CorrectFilesystem(void)
{
	int fileDiagLen = 0;
	if(fatDiagnostics[0])
		fileDiagLen = strlen(fatDiagnostics);

	return fat_eraseAndRemountPartition(eFAT_ID_DISK, fatDiagnostics, FAT_DIAG_BUF_SIZE, fileDiagLen);
}


static FILE *testDiskFile = NULL;
bool fat_Factorytest_CreateFile(void)
{
	if (!fatIsMounted()) {
		ESP_LOGE(TAG, "failed to mount disk");
		return false;
	}

	testDiskFile = fopen("/disk/testdisk.bin", "wb+");

	ESP_LOGW(TAG, "Create file errno: %i: %s", errno, strerror(errno));

	int fatDiagLen = 0;
	if(fatDiagnostics[0])
		fatDiagLen = strlen(fatDiagnostics);

	if(testDiskFile == NULL)
	{
		snprintf(fatDiagnostics + fatDiagLen, FAT_DIAG_BUF_SIZE - fatDiagLen, " Disk file = NULL, %i:%s,", errno, strerror(errno));
		return false;
	}
	else
	{
		snprintf(fatDiagnostics + fatDiagLen, FAT_DIAG_BUF_SIZE - fatDiagLen, " Disk file = 0x%08x", (unsigned int)testDiskFile);
		fclose(testDiskFile);
	}

	return true;
}

bool fat_Factorytest_DeleteFile()
{
	if (!fatIsMounted()) {
		ESP_LOGE(TAG, "failed to mount disk");
		return false;
	}

	int status = remove("/disk/testdisk.bin");
	ESP_LOGW(TAG, "Status from remove: %i", status);

	if(status == 0)
	{
		return true;
	}

	return false;
}

int fat_list_directory(const char * directory_path, cJSON * result){
	if(directory_path == NULL || result == NULL)
		return EINVAL;

	char result_line[128];

	DIR * dir = opendir(directory_path);
	if(dir == NULL){
		snprintf(result_line, sizeof(result_line), "Unable to open '%s' directory: %s", directory_path, strerror(errno));
		cJSON_AddStringToObject(result, "error", result_line);
		return errno;
	}

	errno = 0;

	struct dirent * dp = readdir(dir);
	cJSON * list = cJSON_CreateArray();
	if(list == NULL){
		ESP_LOGE(TAG, "Unable to create directory list");
		return ENOMEM;
	}

	while(dp != NULL){
		if(dp->d_type == DT_REG || dp->d_type == DT_DIR){
			snprintf(result_line, sizeof(result_line), "%-9s: %.12s", (dp->d_type == DT_REG) ? "File" : "Directory", dp->d_name);
			if(cJSON_AddItemToArray(list, cJSON_CreateString(result_line)) != true){
				ESP_LOGE(TAG, "Unable to add entry to directory listing");
			}
		}
		dp = readdir(dir);
	}

	if(errno != 0){
		snprintf(result_line, sizeof(result_line), "Unable to get next directory entry: %s", strerror(errno));
		cJSON_AddStringToObject(result, "error", result_line);
	}

	closedir(dir);
	if(cJSON_AddItemReferenceToObject(result, "entries", list) != true){
		ESP_LOGE(TAG, "Unable to add directory list to result");

		cJSON_Delete(list);
		return ENOMEM;
	}

	return 0;
}
