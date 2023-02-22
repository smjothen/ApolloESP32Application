#include <stdbool.h>
#include "cJSON.h"
#include "esp_err.h"

void fat_make();

bool fat_static_mount();
bool fatIsMounted();
void fat_WriteCertificateBundle(char * newCertificateBundle);
void fat_ReadCertificateBundle(char * readCertificateBundle);
void fat_DeleteCertificateBundle();
void fat_static_unmount();
esp_err_t fat_eraseAndRemountPartition();
int fat_list_directory(const char * directory_path, cJSON * result);
