
// this implementation is based on https://github.com/ZaptecCharger/HANAdapterApplication/blob/db0b3c8042a78e5d8003028c99237b22d219928b/main/https_client.c#L197
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include <stdio.h>
#include <ctype.h>
#include <time.h>
#include "../../main/DeviceInfo.h"
// do we need all these?
// #include "crypto/sha256.h"
#include "base64.h"
#include "sha256.h"
#include "storage.h"
#include "DeviceInfo.h"
#include "rfc3986.h"

// #include "wpa/includes.h"
// #include "crypto/crypto.h"

#include "sas_token.h"

#define TAG "SAS TOKEN GEN  "

int create_sas_token(int ttl_s, char * uniqueId, char * psk, char * token_out){

    //Base64 decoded key for creating signature

	ESP_LOGI(TAG, "Generating token");

    //temp dummy values
    unsigned char key[45];// = {97,98,99,100,97,98,99,100,97,98,99,100,97,98,99,100,97,98,99,100,97,98,99,100,97,98,99,100,97,98,99,100,97,98,99,100,97,98,99,100,97,98,99,100,0};

    memcpy(key, psk, 45);

    time_t UnixTimeStamp = 1592397520;
    time(&UnixTimeStamp);

	//ESP_LOGI(TAG, "psk is: %s(l) and the time is %" PRId32 "", key, UnixTimeStamp);
	size_t key_len = sizeof(key)-1; //Key length -1 end of line char
	size_t base64_key_len;
	unsigned char *base64_key = base64_decode((char *)key, key_len, &base64_key_len);

	//Data for creating signature
	char unixTime[12];
	int tokenValidTime = ttl_s;
	sprintf(unixTime, "%" PRId64 "", UnixTimeStamp+tokenValidTime);
	//ESP_LOGI(TAG, "Unixtime is: %" PRId32 "", UnixTimeStamp);
	//ESP_LOGI(TAG, "Unixtime is: %i", tokenValidTime);
	//ESP_LOGI(TAG, "Unixtime is: %s", unixTime);


#ifdef DEVELOPEMENT_URL
	unsigned char *data = malloc(56+4);
	strcpy((char*)data, "zap-d-iothub.azure-devices.net/devices/");
	const size_t data_len = 55+4; //sizeof(data) gives 4 instead of 56, so cant use sizeof(data)-1 here.
#else
	unsigned char *data;
	size_t data_len_tmp;
	if(storage_Get_ConnectToPortalType() == PORTAL_TYPE_DEV)
	{
		data = malloc(56+4);
		strcpy((char*)data, "zap-d-iothub.azure-devices.net/devices/");
		data_len_tmp = 55+4; //sizeof(data) gives 4 instead of 56, so cant use sizeof(data)-1 here.
	}
	else
	{
		data = malloc(56);
		strcpy((char*)data, "ZapCloud.azure-devices.net/devices/");
		data_len_tmp = 55; //sizeof(data) gives 4 instead of 56, so cant use sizeof(data)-1 here.
	}
	const size_t data_len = data_len_tmp;
#endif


	strcat((char*)data, uniqueId);
	strcat((char*)data, "\n");
	strcat((char*)data, unixTime);

	//hmac-sha256
	unsigned char mac[33] = "";
	unsigned char keybuffer[base64_key_len];
	memcpy(keybuffer, base64_key, base64_key_len);
	//ESP_LOGI(TAG, "base64 key is: %s", keybuffer);
	hmac_sha256(keybuffer, base64_key_len, data, data_len, mac);

	//Base64 encode the returned token
	size_t token_len;
	size_t mac_len = 44; //!= 0 and modulo 4 == 0 (base64_encode gives error if not)
	char *tokenstring = base64_encode(mac, mac_len, &token_len);
	char tokenbuf[45];
	memcpy(tokenbuf, tokenstring, 44);
	//Last character in base64 encoded token should be "=" and should end with 0
	tokenbuf[43] = '=';
	tokenbuf[44] = 0;

	// //Uri-encode token
	size_t bufsize = (strlen(tokenbuf) *3) +1;
	char enc[bufsize];
	memset(enc, 0, bufsize);
	rfc3986_percent_encode((unsigned char *)tokenbuf, enc);
    //base64_url_encode()

	//Build up signature string
#ifdef DEVELOPEMENT_URL
	strcpy(token_out, "SharedAccessSignature sr=zap-d-iothub.azure-devices.net/devices/");
#else
	if(storage_Get_ConnectToPortalType() == PORTAL_TYPE_DEV)
	{
		strcpy(token_out, "SharedAccessSignature sr=zap-d-iothub.azure-devices.net/devices/");
	}
	else
	{
		strcpy(token_out, "SharedAccessSignature sr=ZapCloud.azure-devices.net/devices/");
	}
#endif

	strcat(token_out, uniqueId);
	strcat(token_out, "&sig=");
	//strcat(token_out, tokenbuf);//
    strcat(token_out, enc);
	strcat(token_out, "&se=");
	strcat(token_out, unixTime);
	ESP_LOGI(TAG, "token_out: %s", token_out);

	free(base64_key);
	free(tokenstring);
	free(data);

    return 0;
}
