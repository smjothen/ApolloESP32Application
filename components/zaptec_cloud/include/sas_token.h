#ifndef SAS_TOKEN_H
#define SAS_TOKEN_H

//Moved "#define DEVELOPEMENT_URL" to main/DeviceInfo.h

int create_sas_token(int ttl_s, char * uniqueId, char * psk, char * token_out);

#endif /* SAS_TOKEN_H */
