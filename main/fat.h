#include <stdbool.h>
#include "esp_err.h"

void fat_make();

bool fat_static_mount();
bool fatIsMounted();
void fat_WriteCertificateBundle(char * newCertificateBundle);
void fat_ReadCertificateBundle(char * readCertificateBundle);
void fat_DeleteCertificateBundle();
void fat_static_unmount();
void fat_ClearDiagnostics();
char * fat_GetDiagnostics();
bool fat_eraseAndRemountPartition();
bool fat_CheckFilesSystem();
bool fat_Factorytest_CreateFile();
bool fat_Factorytest_DeleteFile();
