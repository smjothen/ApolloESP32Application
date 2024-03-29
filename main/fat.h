#ifndef FAT_H
#define FAT_H

#include <stdbool.h>
#include <stdio.h>
#include "cJSON.h"
#include "esp_err.h"

enum fat_id{
	eFAT_ID_DISK = 0,
	eFAT_ID_FILES = 1
};


void fat_make(void);

void fat_static_mount(void);
esp_err_t fat_mount(enum fat_id id);

bool fatIsMounted(void);
void fat_WriteCertificateBundle(char * newCertificateBundle);
void fat_ReadCertificateBundle(char * readCertificateBundle);
void fat_DeleteCertificateBundle(void);

void fat_static_unmount(void);
int fat_unmount(enum fat_id id);
void fat_ClearDiagnostics(void);
char * fat_GetDiagnostics(void);
bool fat_CheckFilesSystem(void);
bool fat_CorrectFilesystem(void);
bool fat_Factorytest_CreateFile(void);
bool fat_Factorytest_DeleteFile(void);
esp_err_t fat_fix_and_log_result(enum fat_id id, char * result_log, size_t log_size);
int fat_list_directory(const char * directory_path, cJSON * result);

void fat_disable_mounting(enum fat_id id, bool disable);

esp_err_t fat_eraseAndRemountPartition(enum fat_id id, char * diagBuf, int diagBufMaxSize, int diagBufUsedLen);

#endif /*FAT_H*/
