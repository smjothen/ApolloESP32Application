
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
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "esp_system.h"
#include "freertos/task.h"
#include "DeviceInfo.h"
#include "storage.h"
#include "zaptec_cloud_observations.h"

static const char *TAG = "FAT            ";

// Handle of the wear levelling library instance
static wl_handle_t s_wl_handle = WL_INVALID_HANDLE;

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

void fat_static_mount()
{

	const char * base_paths[2] = {"/disk","/files"}; // partition label with '/' prefix
	const int max_files[2] = {256, 256};

	esp_vfs_fat_mount_config_t mount_config = {
		.max_files = 0,
		.format_if_mount_failed = true,
		.allocation_unit_size = CONFIG_WL_SECTOR_SIZE
	};

	ESP_LOGI(TAG, "Mounting FAT partitions");
	ESP_LOGI(TAG, "=======================");

	for(size_t i = 0; i < 2; i++){
		ESP_LOGI(TAG, "%s partition:", base_paths[i]+1); // skip path prefix ('/')

		if(stat(base_paths[i], NULL) == 0){
			ESP_LOGW(TAG, "\tAlready mounted");
			break;
		}

		if(errno != ENOENT && errno != ENODEV){
			ESP_LOGE(TAG, "\tUnexpected stat error: %s", strerror(errno));
		}
		mount_config.max_files = max_files[i];

		esp_err_t err = esp_vfs_fat_spiflash_mount(base_paths[i], base_paths[i]+1, &mount_config, &s_wl_handle);
		if (err != ESP_OK) {
			ESP_LOGE(TAG, "\tMount failed: (%s)", esp_err_to_name(err));
		}else{
			ESP_LOGI(TAG, "\tMount success");
		}
	}
}

void fat_WriteCertificateBundle(char * newCertificateBundle)
{
	struct stat st;
	if(stat("/disk", &st) != 0)
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

	struct stat st;
	if(stat("/disk", &st) != 0)
	{
		ESP_LOGE(TAG, "Partition not mounted for reading");
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

void fat_DeleteCertificateBundle()
{

	struct stat st;
	if(stat("/disk", &st) != 0)
	{
		ESP_LOGE(TAG, "Partition not mounted for reading");
		return;
	}

    // Open file for reading
    ESP_LOGI(TAG, "Reading file");
    int ret = remove("/disk/cert.txt");

    ESP_LOGI(TAG, "Removed cert file returned: %d", ret);
}

int fat_replace_file(const char * new_file, const char * old_file){
	int err = remove(old_file);
	if(err != 0 && errno != ENOENT)
		ESP_LOGE(TAG, "Unable to remove old file '%s': %s", old_file, strerror(errno));

	err = rename(new_file, old_file);
	if(err != 0){
		ESP_LOGE(TAG, "Unable to replace old file with new '%s': %s", new_file, strerror(errno));
		return -1;
	}
	return 0;
}

int fat_UpdateAuthListFull(int version, struct ocpp_authorization_data ** auth_list, size_t list_length){
	struct stat st;
	if(stat("/disk", &st) != 0)
	{
		ESP_LOGE(TAG, "Partition not mounted for writing");
		return -1;
	}

	ESP_LOGI(TAG, "Creating temporary file for update auth list (Full)");
	errno = 0;
	FILE *f = fopen("/disk/authlist.tmp", "wb");
	if (f == NULL) {
		ESP_LOGE(TAG, "Failed to open file for writing %s", strerror(errno));
		return -1;
	}

	if(fprintf(f, "%d\n", version) < 0){
		fclose(f);
		remove("/disk/authlist.tmp");
		return -1;
	}

	for(size_t i = 0; i < list_length; i++){
		size_t written_items = fwrite(auth_list[i], sizeof(struct ocpp_authorization_data), 1, f);
		if(written_items == 0){
			fclose(f);
			ESP_LOGI(TAG, "Error when writing full auth list. Changes not saved");
			remove("/disk/authlist.tmp");
			return -1;
		}
	}

	fclose(f);

	ESP_LOGI(TAG, "Auth list update complete with %d entries, replacing old file", list_length);
	return fat_replace_file("/disk/authlist.tmp", "/disk/authlist.txt");
}

int fat_UpdateAuthListDifferential(int version, struct ocpp_authorization_data ** auth_list, size_t list_length){
	struct stat st;
	if(stat("/disk", &st) != 0)
	{
		ESP_LOGE(TAG, "Partition not mounted for writing");
		return -1;
	}

	ESP_LOGI(TAG, "Creating temporary file for update auth list (Differential)");
	FILE *f_w = fopen("/disk/authlist.tmp", "wb");
	if (f_w == NULL) {
		ESP_LOGE(TAG, "Failed to open file for writing");
		return -1;
	}

	FILE *f_r = fopen("/disk/authlist.txt", "rb");
	if (f_r == NULL) {
		fclose(f_w);
		ESP_LOGE(TAG, "Failed to open old auth list");
		return -1;
	}

	int version_old;
	fscanf(f_r, "%d\n", &version_old);

	if(ferror(f_r) != 0){
		fclose(f_w);
		fclose(f_r);
		return -1;
	}

	if(fprintf(f_w, "%d\n", version) < 0){ // write version number
		fclose(f_w);
		fclose(f_r);
		return -1;
	}

	struct ocpp_authorization_data auth_data_read;
	bool * auth_list_item_is_written = calloc(sizeof(bool), list_length);

	//TODO: consider improving performance by changing data stucture to remove written items from aut_list
	while(fread(&auth_data_read, sizeof(struct ocpp_authorization_data), 1, f_r) == 1){
		bool is_match = false;
		for(size_t i = 0; i < list_length; i++){
			if(strcasecmp(auth_data_read.id_tag, auth_list[i]->id_tag) == 0){
				is_match = true;

				if(strcmp(auth_list[i]->id_tag_info.status, "DELETE") != 0)
					fwrite(auth_list[i], sizeof(struct ocpp_authorization_data), 1, f_w); // write authorization data that is in both auth_list and on file

				auth_list_item_is_written[i] = true;
				break;
			}
		}

		if(!is_match){
			fwrite(&auth_data_read, sizeof(struct ocpp_authorization_data), 1, f_w); // Write authorization data that is only on file
		}

		if(ferror(f_w) != 0){
			goto error;
		}
	}

	for(size_t i = 0; i < list_length; i++){
		if(auth_list_item_is_written[i] == false){
			if(strcmp(auth_list[i]->id_tag_info.status, "DELETE") != 0)
				fwrite(auth_list[i], sizeof(struct ocpp_authorization_data), 1, f_w); // Write authorization data that is only in auth_list

			if(ferror(f_w) != 0){
				goto error;
			}
		}
	}

	fclose(f_w);
	fclose(f_r);
	free(auth_list_item_is_written);

	ESP_LOGI(TAG, "Auth list update complete, replacing old file");
	return fat_replace_file("/disk/authlist.tmp", "/disk/authlist.txt");
error:

	ESP_LOGE(TAG, "Error while writing auth list");
	fclose(f_w);
	fclose(f_r);
	free(auth_list_item_is_written);
	return -1;
}

bool fat_ReadAuthData(const char * id_tag, struct ocpp_authorization_data * auth_data_out){ // TODO: Test
	struct stat st;
	if(stat("/disk", &st) != 0)
	{
		ESP_LOGE(TAG, "Partition not mounted for writing");
		return false;
	}

	ESP_LOGI(TAG, "Getting auth data from file given tag");
	FILE *f = fopen("/disk/authlist.txt", "rb");
	if (f == NULL) {
		ESP_LOGE(TAG, "Failed to open file for reading");
		return false;
	}

	int version;
	fscanf(f, "%d\n", &version);
	if(ferror(f) != 0){
		fclose(f);
		return false;
	}

	bool found = false;

	while(fread(auth_data_out, sizeof(struct ocpp_authorization_data), 1, f) == 1){
		if(strcasecmp(auth_data_out->id_tag, id_tag) == 0){
			found = true;
			break;
		}
	}

	ESP_LOGI(TAG, "ID tag %s: feof (%d), ferror (%d)", found ? "Found" : "Not found", feof(f), ferror(f));
	if(ferror(f) != 0)
		ESP_LOGE(TAG, "Error while reading auth file: %d", ferror(f));

	fclose(f);

	return found;
}

int fat_ReadAuthListVersion(){
	struct stat st;
	if(stat("/disk", &st) != 0)
	{
		ESP_LOGE(TAG, "Partition not mounted for writing");
		return -1;
	}

        FILE *f = fopen("/disk/authlist.txt", "rb");
	if (f == NULL) {
		ESP_LOGE(TAG, "Failed to open file for reading version");
		return -1;
	}

	int version;
	fscanf(f, "%d\n", &version);

	if(ferror(f) != 0)
		version = -1;

	fclose(f);

	return version;
}

