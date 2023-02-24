
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

	return esp_vfs_fat_spiflash_mount(base_paths[id], base_paths[id]+1, &mount_config, &s_wl_handle);
}

void fat_static_mount()
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

void fat_static_unmount()
{
	ESP_LOGI(TAG, "Unmounting FAT partitions");
	ESP_LOGI(TAG, "=======================");

	for(size_t i = 0; i < PARTITION_COUNT; i++){
		ESP_LOGI(TAG, "%s partition:", base_paths[i]+1);
		if(esp_vfs_fat_spiflash_unmount(base_paths[i], s_wl_handle) != ESP_OK){
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

	return esp_vfs_fat_spiflash_unmount(base_paths[id], s_wl_handle);
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

int fat_UpdateAuthListFull(int version, struct ocpp_authorization_data * auth_list, size_t list_length){
	struct stat st;
	if(stat("/disk", &st) != 0)
	{
		ESP_LOGE(TAG, "Partition not mounted for writing");
		return -1;
	}

	if(list_length == 0){
		if(stat("/disk/authlist.txt", &st) == 0){
			return remove("/disk/authlist.txt");
		}else{
			return 0;
		}
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
		size_t written_items = fwrite(&auth_list[i], sizeof(struct ocpp_authorization_data), 1, f);
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

int fat_UpdateAuthListDifferential(int version, struct ocpp_authorization_data * auth_list, size_t list_length){
	struct stat st;
	if(stat("/disk", &st) != 0)
	{
		ESP_LOGE(TAG, "Partition not mounted for writing");
		return -1;
	}

	bool * auth_list_item_is_written = NULL;

	ESP_LOGI(TAG, "Creating temporary file for update auth list (Differential)");
	FILE *f_w = fopen("/disk/authlist.tmp", "wb");
	if (f_w == NULL) {
		ESP_LOGE(TAG, "Failed to open file for writing");
		return -1;
	}

	if(fprintf(f_w, "%d\n", version) < 0){ // write version number
		ESP_LOGE(TAG, "Unable to write version during differential update");

		goto error;
	}

	auth_list_item_is_written = calloc(sizeof(bool), list_length);
	if(auth_list_item_is_written == NULL){
		ESP_LOGE(TAG, "Unable to allocate buffer to track written items during differential update");

		goto error;
	}

	if(stat("/disk/authlist.txt", &st) == 0){ // If old auth list exists

		FILE *f_r = fopen("/disk/authlist.txt", "rb");
		if (f_r == NULL) {
			ESP_LOGE(TAG, "Failed to open old auth list");

			goto error;
		}

		int version_old;
		fscanf(f_r, "%d\n", &version_old);

		if(ferror(f_r) != 0){
			fclose(f_r);

			goto error;
		}

		struct ocpp_authorization_data auth_data_read;

		//TODO: consider improving performance by changing data stucture to remove written items from aut_list
		while(fread(&auth_data_read, sizeof(struct ocpp_authorization_data), 1, f_r) == 1){
			bool is_match = false;
			for(size_t i = 0; i < list_length; i++){
				if(strcasecmp(auth_data_read.id_tag, auth_list[i].id_tag) == 0){
					is_match = true;

					if(strcmp(auth_list[i].id_tag_info.status, "DELETE") != 0)
						fwrite(&auth_list[i], sizeof(struct ocpp_authorization_data), 1, f_w); // write authorization data that is in both auth_list and on file

					auth_list_item_is_written[i] = true;
					break;
				}
			}

			if(!is_match){
				fwrite(&auth_data_read, sizeof(struct ocpp_authorization_data), 1, f_w); // Write authorization data that is only on file
			}

			if(ferror(f_w) != 0){
				fclose(f_r);

				goto error;
			}
		}

		fclose(f_r);
	}

	for(size_t i = 0; i < list_length; i++){
		if(auth_list_item_is_written[i] == false && strcmp(auth_list[i].id_tag_info.status, "DELETE") != 0){
			if(fwrite(&auth_list[i], sizeof(struct ocpp_authorization_data), 1, f_w) != 1){ // Write authorization data that is only in auth_list
				ESP_LOGE(TAG, "Unable to write new list items during differential update");

				goto error;
			}
		}
	}

	fclose(f_w);
	free(auth_list_item_is_written);

	ESP_LOGI(TAG, "Auth list update complete, replacing old file");
	return fat_replace_file("/disk/authlist.tmp", "/disk/authlist.txt");
error:

	ESP_LOGE(TAG, "Error while writing auth list");
	fclose(f_w);
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

	if(stat("/disk/authlist.txt", &st) != 0){
		return 0;
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

esp_err_t fat_eraseAndRemountPartition(enum fat_id id)
{
	if(id >= PARTITION_COUNT)
		return ESP_ERR_INVALID_ARG;

	esp_err_t err = ESP_OK;

	const esp_partition_t * part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, base_paths[id]+1);

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
	return err;
}

const char * test_file_name = "fat_test.bin";

int fat_create_test_file(enum fat_id id){
	char file_path[32];

	if(id >= PARTITION_COUNT)
		return EINVAL;

	int written = sprintf(file_path, "%s/%s", base_paths[id], test_file_name);
	if(written < 0){
		return errno;
	}

	FILE * fp = fopen(file_path, "wb");

	if(fp == NULL)
		return errno;

	if(fclose(fp) != 0)
		return errno;

	return 0;
}

int fat_remove_test_file(enum fat_id id){
	char file_path[32];

	if(id >= PARTITION_COUNT)
		return EINVAL;

	int written = sprintf(file_path, "%s/%s", base_paths[id], test_file_name);
	if(written < 0){
		return errno;
	}

	if(remove(file_path) != 0)
		return errno;

	return 0;
}

esp_err_t fat_fix_and_log_result(enum fat_id id, char * result_log, size_t log_size){

	if(id >= PARTITION_COUNT)
		return ESP_ERR_INVALID_ARG;

	ESP_LOGI(TAG, "Attempting to fix partition '%s'", base_paths[id]+1);

	size_t offset = 0;
	const esp_partition_t *part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, base_paths[id]+1);

	if(part == NULL)
		return ESP_ERR_NOT_FOUND;

	ESP_LOGI(TAG, "Creating test file");
	int err = fat_create_test_file(id);
	if(err != 0)
		ESP_LOGE(TAG, "Failed to create file: %s", strerror(err));
	int written = snprintf(result_log + offset, log_size - offset, "Create file: %d", err);
	if(written < 0){
		ESP_LOGE(TAG, "Unable to log creation result");
	}else{
		offset += written;
		if(offset > log_size)
			offset = log_size;
	}

	ESP_LOGI(TAG, "Unmounting files-filesystem");
	esp_err_t esp_err = fat_unmount(id);
	if(esp_err != ESP_OK)
		ESP_LOGE(TAG, "Failed to unmount: %s", esp_err_to_name(esp_err));
	written = snprintf(result_log + offset, log_size - offset, " Unm: %d", esp_err);
	if(written < 0){
		ESP_LOGE(TAG, "Unable to log unmount result");
	}else{
		offset += written;
		if(offset > log_size)
			offset = log_size;
	}

	ESP_LOGI(TAG, "Erasing partition");
	esp_err = esp_partition_erase_range(part, 0, part->size);
	if(esp_err != ESP_OK)
		ESP_LOGE(TAG, "Erase failed: %s", esp_err_to_name(esp_err));
	written = snprintf(result_log + offset, log_size - offset, " Erase: %d", esp_err);
	if(written < 0){
		ESP_LOGE(TAG, "Unable to log erase result");
	}else{
		offset += written;
		if(offset > log_size)
			offset = log_size;
	}

	ESP_LOGI(TAG, "Re-mounting partition");
	esp_err = fat_mount(id);
	if(err != 0)
		ESP_LOGE(TAG, "Failed to mount: %s", esp_err_to_name(esp_err));
	written = snprintf(result_log + offset, log_size - offset, " M: %d", err);
	if(written < 0){
		ESP_LOGE(TAG, "Unable to log mount result");
	}else{
		offset += written;
		if(offset > log_size)
			offset = log_size;
	}

	ESP_LOGI(TAG, "Re-creating test file");
	err = fat_create_test_file(id);
	written = snprintf(result_log + offset, log_size - offset, " Create file: %d", err);
	if(written < 0){
		ESP_LOGE(TAG, "Unable to log re-creation result");
	}else{
		offset += written;
		if(offset > log_size)
			offset = log_size;
	}


	ESP_LOGI(TAG, "Removing test file");
	err = fat_remove_test_file(id);
	written = snprintf(result_log + offset, log_size - offset, " Remove file: %d", err);
	if(written < 0){
		ESP_LOGE(TAG, "Unable to log removal result");
	}else{
		offset += written;
		if(offset > log_size)
			offset = log_size;
	}

	return esp_err;
}
