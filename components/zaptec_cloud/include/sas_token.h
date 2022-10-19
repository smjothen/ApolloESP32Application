#ifndef SAS_TOKEN_H
#define SAS_TOKEN_H

//#define DEVELOPEMENT_URL
// Not yet used #define DEVELOPEMENT_DOWNLOAD_URL

//int create_sas_token(int ttl_s, char * token_out);
int create_sas_token(int ttl_s, char * uniqueId, char * psk, char * token_out);

#endif /* SAS_TOKEN_H */
