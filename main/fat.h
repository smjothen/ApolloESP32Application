#ifndef FAT_H
#define FAT_H

#include <stdbool.h>
#include <stdio.h>
#include "../components/ocpp/include/types/ocpp_authorization_data.h"

void fat_make();

bool fat_static_mount();
bool fatIsMounted();

void fat_WriteCertificateBundle(char * newCertificateBundle);
void fat_ReadCertificateBundle(char * readCertificateBundle);
void fat_DeleteCertificateBundle();

int fat_UpdateAuthListFull(int version, struct ocpp_authorization_data ** auth_data, size_t list_length);
int fat_UpdateAuthListDifferential(int version, struct ocpp_authorization_data ** auth_data, size_t list_length);
bool fat_ReadAuthData(const char * id_token, struct ocpp_authorization_data * auth_data_out);
int fat_ReadAuthListVersion();

void fat_static_unmount();

#endif /*FAT_H*/
