#include <stdio.h>
#include <string.h>

#if defined(__aarch64__)
#define ESP_LOGE(tag, fmt, ...) printf(fmt "\n", ## __VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) printf(fmt "\n", ## __VA_ARGS__)
#include <assert.h>
#else
#include "esp_log.h"
#endif

#include "mbedtls/ecdsa.h"
#include "mbedtls/pk.h"
#include "mbedtls/sha256.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"
#include "mbedtls/base64.h"

#include "mid_sign.h"

static const char *TAG = "MID            ";

static mid_sign_ctx_t ctx = {0};

mid_sign_ctx_t *mid_sign_ctx_get_global(void) {
	return &ctx;
}

int mid_sign_ctx_init(mid_sign_ctx_t *ctx, char *prv_buf, size_t prv_size, char *pub_buf, size_t pub_size) {
	mbedtls_ctr_drbg_init(&ctx->ctr_drbg);
	mbedtls_entropy_init(&ctx->entropy);
	mbedtls_pk_init(&ctx->key);
	mbedtls_ecdsa_init(&ctx->ecdsa);

	ctx->flag = 0;

	int ret;

	if ((ret = mbedtls_ctr_drbg_seed(&ctx->ctr_drbg, mbedtls_entropy_func, &ctx->entropy, NULL, 0)) != 0) {
		ESP_LOGE(TAG, "mbedtls_ctr_drbg_seed returned -0x%04x", (unsigned int) -ret);
		goto error;
	}

	if ((ret = mbedtls_pk_parse_key(&ctx->key, (unsigned char *)prv_buf, strlen(prv_buf) + 1, NULL, 0, mbedtls_ctr_drbg_random, &ctx->ctr_drbg)) != 0) {
		ESP_LOGE(TAG, "mbedtls_pk_parse_key returned -0x%04x", (unsigned int) -ret);

		// Couldn't parse, probably no key stored so generate...
		if ((ret = mbedtls_pk_setup(&ctx->key, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY))) != 0) {
			ESP_LOGE(TAG, "mbedtls_pk_setup returned -0x%04x", (unsigned int) -ret);
			goto error;
		}

		if ((ret = mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP384R1, mbedtls_pk_ec(ctx->key), mbedtls_ctr_drbg_random, &ctx->ctr_drbg)) != 0) {
			ESP_LOGE(TAG, "mbedtls_ecp_gen_key returned -0x%04x", (unsigned int) -ret);
			goto error;
		}

		if ((ret = mbedtls_pk_write_key_pem(&ctx->key, (unsigned char *)prv_buf, prv_size)) != 0) {
			ESP_LOGE(TAG, "mbedtls_pk_write_key_pem returned -0x%04x", (unsigned int) -ret);
			goto error;
		}

		if ((ret = mbedtls_pk_write_pubkey_pem(&ctx->key, (unsigned char *)pub_buf, pub_size)) != 0) {
			ESP_LOGE(TAG, "mbedtls_pk_write_pubkey_pem returned -0x%04x", (unsigned int) -ret);
			goto error;
		}

		ctx->flag |= MID_SIGN_FLAG_GENERATED;
	}

	if ((ret = mbedtls_ecdsa_from_keypair(&ctx->ecdsa, mbedtls_pk_ec(ctx->key))) != 0) {
		ESP_LOGE(TAG, "mbedtls_ecdsa_from_keypair returned -0x%04x", (unsigned int) -ret);
		goto error;
	}

	ctx->flag |= MID_SIGN_FLAG_INITIALIZED;

	mbedtls_pk_context pub_key;
	mbedtls_pk_init(&pub_key);

	if ((ret = mbedtls_pk_parse_public_key(&pub_key, (unsigned char *)pub_buf, strlen(pub_buf) + 1)) != 0) {
		ESP_LOGE(TAG, "mbedtls_pk_parse_public_key returned -0x%04x", (unsigned int) -ret);
		goto error_pub;
	}

	if ((ret = mbedtls_pk_check_pair(&pub_key, &ctx->key, mbedtls_ctr_drbg_random, &ctx->ctr_drbg)) != 0) {
		ESP_LOGE(TAG, "mbedtls_pk_check_pair returned -0x%04x", (unsigned int) -ret);
		goto error_pub;
	}

	ctx->flag |= MID_SIGN_FLAG_VERIFIED;

	mbedtls_pk_free(&pub_key);
	return 0;

error_pub:
	mbedtls_pk_free(&pub_key);
error:
	mbedtls_pk_free(&ctx->key);
	mbedtls_ecdsa_free(&ctx->ecdsa);
	return -1;
}

int mid_sign_ctx_free(mid_sign_ctx_t *ctx) {
	mbedtls_ctr_drbg_free(&ctx->ctr_drbg);
	mbedtls_entropy_free(&ctx->entropy);
	mbedtls_pk_free(&ctx->key);
	mbedtls_ecdsa_free(&ctx->ecdsa);
	memset(ctx, 0, sizeof (*ctx));

	return 0;
}

/*
int mid_sign_ctx_get_private_key(mid_sign_ctx_t *ctx, char *buf, size_t buf_size) {
	int ret;
	if ((ret = mbedtls_pk_write_key_pem(&ctx->key, (unsigned char *)buf, buf_size)) != 0) {
		ESP_LOGE(TAG, "mbedtls_pk_write_key_pem returned -0x%04x", (unsigned int) -ret);
		return -1;
	}

	return 0;
}
*/

int mid_sign_ctx_get_public_key(mid_sign_ctx_t *ctx, char *buf, size_t buf_size) {
	int ret;
	if ((ret = mbedtls_pk_write_pubkey_pem(&ctx->key, (unsigned char *)buf, buf_size)) != 0) {
		ESP_LOGE(TAG, "mbedtls_pk_write_pubkey_pem returned -0x%04x", (unsigned int) -ret);
		return -1;
	}

	return 0;
}

// Signs and returns signature in base64 encoded buffer
int mid_sign_ctx_sign(mid_sign_ctx_t *ctx, char *str, size_t str_len, char *sig64, size_t *sig64_len) {
	int ret;

	unsigned char sig[MBEDTLS_ECDSA_MAX_LEN];
	size_t sig_len;

	unsigned char hash[32];
	unsigned char *msg = (unsigned char *)str;

	if ((ret = mbedtls_sha256(msg, str_len, hash, 0)) != 0) {
		ESP_LOGE(TAG, "mbedtls_sha256 returned -0x%04x\n", (unsigned int) -ret);
		return -1;
	}

	if ((ret = mbedtls_ecdsa_write_signature(&ctx->ecdsa, MBEDTLS_MD_SHA256, hash, sizeof (hash), sig, sizeof (sig), &sig_len,
					mbedtls_ctr_drbg_random, &ctx->ctr_drbg)) != 0) {
		ESP_LOGE(TAG, "mbedtls_ecdsa_write_signature returned -0x%04x", (unsigned int) -ret);
		return -1;
	}

	size_t out_len;
	if ((ret = mbedtls_base64_encode((unsigned char *)sig64, *sig64_len, &out_len, (const unsigned char *)sig, sig_len)) != 0) {
		ESP_LOGE(TAG, "mbedtls_base64_encode returned -0x%04x", (unsigned int) -ret);
		return -1;
	}

	*sig64_len = out_len;
	return 0;
}

int mid_sign_ctx_verify(mid_sign_ctx_t *ctx, char *str, size_t str_len, char *sig64, size_t sig64_len) {
	int ret;

	unsigned char hash[32];
	unsigned char *msg = (unsigned char *)str;

	unsigned char sig[MBEDTLS_ECDSA_MAX_LEN];
	size_t sig_len;

	if ((ret = mbedtls_sha256(msg, str_len, hash, 0)) != 0) {
		ESP_LOGE(TAG, "mbedtls_sha256 returned -0x%04x\n", (unsigned int) -ret);
		return -1;
	}

	if ((ret = mbedtls_base64_decode((unsigned char *)sig, MBEDTLS_ECDSA_MAX_LEN, &sig_len, (const unsigned char *)sig64, sig64_len)) != 0) {
		ESP_LOGE(TAG, "mbedtls_base64_decode returned -0x%04x", (unsigned int) -ret);
		return -1;
	}

	if ((ret = mbedtls_ecdsa_read_signature(&ctx->ecdsa, hash, sizeof (hash), sig, sig_len)) != 0) {
		ESP_LOGE(TAG, "mbedtls_ecdsa_read_signature returned -0x%04x", (unsigned int) -ret);
		return -1;
	}

	return 0;
}

