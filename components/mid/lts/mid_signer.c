#include <stdio.h>
#include <string.h>
#include <assert.h>

#define ESP_LOGE(tag, fmt, ...) printf(fmt "\n", __VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) printf(fmt "\n", __VA_ARGS__)

#include "mbedtls/ecdsa.h"
#include "mbedtls/pk.h"
#include "mbedtls/sha256.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"
#include "mbedtls/base64.h"

#include "mid_sign.h"

int main(void) {
	char pub[512];
	char prv[512];

	mid_sign_ctx_t ctx = {0};

	int ret;
	if ((ret = mid_sign_ctx_generate(prv, sizeof (prv), pub, sizeof (pub))) != 0) {
		ESP_LOGI(TAG, "Generate failure: %d", ret);
		return -1;
	}

	printf("%s%s", prv, pub);

	if ((ret = mid_sign_ctx_init(&ctx, prv, pub)) != 0) {
		ESP_LOGI(TAG, "Init failure: %d", ret);
		return -1;
	}

	printf("%x\n", ctx.flag);

	char tmp = prv[128];
	// Test mangled input
	prv[128] = 0xca;

	if ((ret = mid_sign_ctx_init(&ctx, prv, pub)) == 0) {
		ESP_LOGI(TAG, "Init success with bad input!: %d", ret);
		return -1;
	}

	printf("%s%s", prv, pub);
	printf("%x\n", ctx.flag);
	assert(ctx.flag == 0);

	prv[128] = tmp;

	if ((ret = mid_sign_ctx_init(&ctx, prv, pub)) != 0) {
		ESP_LOGI(TAG, "Init failure: %d", ret);
		return -1;
	}

	char sig[512];
	size_t sig_len = 512;

	if ((ret = mid_sign_ctx_sign(&ctx, "test", 4, sig, &sig_len)) != 0) {
		ESP_LOGI(TAG, "Signing failed: %d", ret);
		return -1;
	}

	printf("Signature (len %zu): %s\n", sig_len, sig);

	// Test good input
	if ((ret = mid_sign_ctx_verify(&ctx, "test", 4, sig, sig_len)) != 0) {
		ESP_LOGI(TAG, "Verify failed: %d", ret);
		return -1;
	}

	// Test bad input
	if ((ret = mid_sign_ctx_verify(&ctx, "testtest", 8, sig, sig_len)) == 0) {
		ESP_LOGI(TAG, "Verify failed: %d", ret);
		return -1;
	}

	return 0;
}
