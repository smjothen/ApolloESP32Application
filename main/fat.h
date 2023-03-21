#ifndef FAT_H
#define FAT_H

#include <stdbool.h>
#include <stdio.h>
#include "../components/ocpp/include/types/ocpp_authorization_data.h"
#include "esp_err.h"

enum fat_id{
	eFAT_ID_DISK = 0,
	eFAT_ID_FILES = 1
};

void fat_make();

void fat_static_mount();
esp_err_t fat_mount(enum fat_id id);

void fat_WriteCertificateBundle(char * newCertificateBundle);
void fat_ReadCertificateBundle(char * readCertificateBundle);
void fat_DeleteCertificateBundle();

void fat_static_unmount();
int fat_unmount(enum fat_id id);

void fat_disable_mounting(enum fat_id id, bool disable);

esp_err_t fat_fix_and_log_result(enum fat_id id, char * result_log, size_t log_size);
esp_err_t fat_eraseAndRemountPartition(enum fat_id id);


#endif /*FAT_H*/
