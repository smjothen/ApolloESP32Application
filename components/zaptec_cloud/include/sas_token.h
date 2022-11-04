#ifndef SAS_TOKEN_H
#define SAS_TOKEN_H

#ifdef CONFIG_ZAPTEC_CLOUD_USE_DEVELOPMENT_URL
#define DEVELOPEMENT_URL
#endif /* CONFIG_ZAPTEC_CLOUD_USE_DEVELOPMENT_URL */

// Not yet used #define DEVELOPEMENT_DOWNLOAD_URL

//int create_sas_token(int ttl_s, char * token_out);
int create_sas_token(int ttl_s, char * uniqueId, char * psk, char * token_out);

#endif /* SAS_TOKEN_H */
