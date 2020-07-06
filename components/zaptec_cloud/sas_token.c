
// this implementation is based on https://github.com/ZaptecCharger/HANAdapterApplication/blob/db0b3c8042a78e5d8003028c99237b22d219928b/main/https_client.c#L197
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include <stdio.h>
#include <ctype.h>
#include <time.h>

// do we need all these?
// #include "crypto/sha256.h"
#include "base64.h"
#include "sha256.h"

// #include "wpa/includes.h"
// #include "crypto/crypto.h"

#include "sas_token.h"

#define TAG "SAS TOKEN GENERATOR"

char rfc3986[256] = {0};
char html5[256] = {0};
void encode(const char *s, char *enc, char *tb)
{
	for (; *s; s++) {
		if (tb[(int)*s]) sprintf(enc, "%c", tb[(int)*s]);
		else        sprintf(enc, "%%%02X", *s);
		while (*++enc);
	}
}

int create_sas_token(int ttl_s, char * token_out){

    //Base64 decoded key for creating signature
	// unsigned char key[45];
	//storage_readFactoryPsk(key);

    //temp dummy values
    unsigned char key[] = {97,98,99,100,97,98,99,100,97,98,99,100,97,98,99,100,97,98,99,100,97,98,99,100,97,98,99,100,97,98,99,100,97,98,99,100,97,98,99,100,97,98,99,100,0};
    //char signed_key[] = "ubTCXZJoEs8LjFw3lVFzSLXQ0CCJDEiNt7AyqbvxwFA=";
    char signed_key[] = "mikfgBtUnIbuoSyCwXjUwgF29KONrGIy5H/RbpGTtdo=";

    memcpy(key, signed_key, sizeof(key));

    time_t UnixTimeStamp = 1592397520;
    time(&UnixTimeStamp);
    //char *uniqueId = "ZAP000001";
    char *uniqueId = "ZAP000002";

	ESP_LOGI(TAG, "psk is: %s(l) and the time is %ld", key, UnixTimeStamp);
	size_t key_len = sizeof(key)-1; //Key length -1 end of line char
	size_t base64_key_len;
	unsigned char *base64_key = base64_decode((char *)key, key_len, &base64_key_len);

	//Data for creating signature
	char unixTime[12];
	int tokenValidTime = ttl_s;
	sprintf(unixTime, "%ld", UnixTimeStamp+tokenValidTime);
	ESP_LOGI(TAG, "Unixtime is: %ld", UnixTimeStamp);
	ESP_LOGI(TAG, "Unixtime is: %i", tokenValidTime);
	ESP_LOGI(TAG, "Unixtime is: %s", unixTime);
	unsigned char *data = malloc(56);
	strcpy((char*)data, "ZapCloud.azure-devices.net/devices/");
	strcat((char*)data, uniqueId);
	strcat((char*)data, "\n");
	strcat((char*)data, unixTime);
	const size_t data_len = 55; //sizeof(data) gives 4 instead of 56, so cant use sizeof(data)-1 here.

	//hmac-sha256
	unsigned char mac[33] = "";
	unsigned char keybuffer[base64_key_len];
	memcpy(keybuffer, base64_key, base64_key_len);
	ESP_LOGI(TAG, "base64 key is: %s", keybuffer);
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
	for (int i = 0; i < 256; i++) {
		rfc3986[i] = isalnum(i)||i == '~'||i == '-'||i == '.'||i == '_'
			? i : 0;
		html5[i] = isalnum(i)||i == '*'||i == '-'||i == '.'||i == '_'
			? i : (i == ' ') ? '+' : 0;
	}
	encode(tokenbuf, enc, rfc3986);
    //base64_url_encode()

	//Build up signature string
	strcpy(token_out, "SharedAccessSignature sr=ZapCloud.azure-devices.net/devices/");
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
