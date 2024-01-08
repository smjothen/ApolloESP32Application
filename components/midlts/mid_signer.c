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

// Compile on host machine:
//
// gcc -I/opt/homebrew/include/ /opt/homebrew/lib/libmbedtls.a /opt/homebrew/lib/libmbedcrypto.a mid_sign.c mid_signer.c -o mid_sign
//

int main(void) {
	char pub[512];
	char prv[512];

	MIDSignCtx ctx = {0};

	int ret;
	if ((ret = mid_sign_ctx_init(&ctx, prv, sizeof (prv), pub, sizeof (pub))) != 0) {
		ESP_LOGI(TAG, "Init failure: %d", ret);
		return -1;
	}

	printf("%s%s", prv, pub);
	printf("%x\n", ctx.flag);
	// Should be initialized + generated + verified
	assert(ctx.flag == 7);

	MIDSignCtx ctx2 = {0};

	if ((ret = mid_sign_ctx_init(&ctx, prv, sizeof (prv), pub, sizeof (pub))) != 0) {
		ESP_LOGI(TAG, "Init failure: %d", ret);
		return -1;
	}

	printf("%s%s", prv, pub);
	printf("%x\n", ctx.flag);
	// Should be initialized + verified
	assert(ctx.flag == 3);

	// Test mangled input
	prv[128] = 0xca;

	MIDSignCtx ctx3 = {0};

	if ((ret = mid_sign_ctx_init(&ctx, prv, sizeof (prv), pub, sizeof (pub))) != 0) {
		ESP_LOGI(TAG, "Init failure: %d", ret);
		return -1;
	}

	printf("%s%s", prv, pub);
	printf("%x\n", ctx.flag);
	// Should be initialized + generated + verified
	assert(ctx.flag == 7);

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
