#include <stdio.h>
#include <string.h>

#include "esp_log.h"

#include "mbedtls/ecdsa.h"
#include "mbedtls/pk.h"
#include "mbedtls/sha256.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"

#include "wpa_supplicant/base64.h"

#include "storage.h"
#include "protocol_task.h"
#include "zaptec_protocol_serialisation.h"

#include "mid_status.h"
#include "mid_sign.h"
#include "mid.h"

static const char *TAG = "MID            ";

static MIDSignCtx ctx;

MIDSignCtx *mid_sign_ctx_get_global(void) {
	return &ctx;
}

int mid_sign_ctx_init(MIDSignCtx *ctx, char *prv_pem, char *pub_pem) {
	mbedtls_ctr_drbg_init(&ctx->ctr_drbg);
	mbedtls_entropy_init(&ctx->entropy);
	mbedtls_pk_init(&ctx->key);

	ctx->flag = 0;

	int ret;
	if ((ret = mbedtls_ctr_drbg_seed(&ctx->ctr_drbg, mbedtls_entropy_func, &ctx->entropy, NULL, 0)) != 0) {
		ESP_LOGE(TAG, "mbedtls_ctr_drbg_seed returned -0x%04x", (unsigned int) -ret);
		goto error;
	}

	if ((ret = mbedtls_pk_parse_key(&ctx->key, (unsigned char *)prv_pem, strlen(prv_pem) + 1, NULL, 0, mbedtls_ctr_drbg_random, &ctx->ctr_drbg)) != 0) {
		ESP_LOGE(TAG, "mbedtls_pk_parse_key returned -0x%04x", (unsigned int) -ret);
		goto error;
	}

	ctx->flag |= MID_SIGN_FLAG_INITIALIZED;

	if (mid_sign_ctx_verify_public_key(ctx, pub_pem) != 0) {
		goto error;
	}

	return 0;

error:
	mbedtls_pk_free(&ctx->key);
	return -1;
}

int mid_sign_ctx_verify_public_key(MIDSignCtx *ctx, char *pub_pem) {
	mbedtls_pk_context pub_key;
	mbedtls_pk_init(&pub_key);

	int ret;
	if ((ret = mbedtls_pk_parse_public_key(&pub_key, (unsigned char *)pub_pem, strlen(pub_pem) + 1)) != 0) {
		ESP_LOGE(TAG, "mbedtls_pk_parse_public_key returned -0x%04x", (unsigned int) -ret);
		goto error;
	}

	if ((ret = mbedtls_pk_check_pair(&pub_key, &ctx->key, mbedtls_ctr_drbg_random, &ctx->ctr_drbg)) != 0) {
		ESP_LOGE(TAG, "mbedtls_pk_check_pair returned -0x%04x", (unsigned int) -ret);
		goto error;
	}

	ctx->flag |= MID_SIGN_FLAG_VERIFIED;

	return 0;

error:
	mbedtls_pk_free(&pub_key);
	return -1;
}

int mid_sign_ctx_generate_key(MIDSignCtx *ctx) {
	mbedtls_pk_context key;
	mbedtls_pk_init(&key);

	int ret;
	if ((ret = mbedtls_pk_setup(&key, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY))) != 0) {
		ESP_LOGE(TAG, "mbedtls_pk_setup returned -0x%04x", (unsigned int) -ret);
		goto error;
	}

	if ((ret = mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP384R1, mbedtls_pk_ec(key), mbedtls_ctr_drbg_random, &ctx->ctr_drbg)) != 0) {
		ESP_LOGE(TAG, "mbedtls_ecp_gen_key returned -0x%04x", (unsigned int) -ret);
		goto error;
	}

	ctx->flag |= MID_SIGN_FLAG_INITIALIZED;

	return 0;

error:
	mbedtls_pk_free(&key);
	return -1;
}

int mid_sign_ctx_get_private_key(MIDSignCtx *ctx, char *buf, size_t buf_size) {
	int ret;
	if ((ret = mbedtls_pk_write_key_pem(&ctx->key, (unsigned char *)buf, buf_size)) != 0) {
		ESP_LOGE(TAG, "mbedtls_pk_write_key_pem returned -0x%04x", (unsigned int) -ret);
		return -1;
	}

	return 0;
}

int mid_sign_ctx_get_public_key(MIDSignCtx *ctx, char *buf, size_t buf_size) {
	int ret;
	if ((ret = mbedtls_pk_write_pubkey_pem(&ctx->key, (unsigned char *)buf, buf_size)) != 0) {
		ESP_LOGE(TAG, "mbedtls_pk_write_pubkey_pem returned -0x%04x", (unsigned int) -ret);
		return -1;
	}

	return 0;
}

/*

	if (mid_keypair_verify() != 0) {
		ESP_LOGE(TAG, "Couldn't verify MID keys, generating ...");
		if (mid_keypair_generate() == 0) {
			ESP_LOGI(TAG, "Successfully generated MID keys!");

			storage_Set_MIDPublicKey((char *)mid_public_key);
			storage_Set_MIDPrivateKey((char *)mid_private_key);
			storage_SaveConfiguration();
		} else {
			ESP_LOGE(TAG, "Failed to generate MID keys!");
			return -1;
		}
	} else {
		ESP_LOGI(TAG, "Verified MID keys!");
	}

	return 0;
}

static int mid_sign(unsigned char *message, size_t len) {
	return 0;
}

static int mid_verify(unsigned char *message, size_t len) {
	return 0;
}

static int mid_keypair_verify(void) {
	mbedtls_pk_context pub_key;

	mbedtls_pk_init(&pub_key);

	size_t key_len = strlen((char *)mid_private_key);
	int ret;

	if ((ret = mbedtls_pk_parse_key(&key, mid_private_key, key_len + 1, NULL, 0, mbedtls_ctr_drbg_random, &ctx->ctr_drbg)) != 0) {
		ESP_LOGE(TAG, "mbedtls_pk_parse_key returned -0x%04x", (unsigned int) -ret);
		return -1;
	}

	key_len = strlen((char *)mid_public_key);
	if ((ret = mbedtls_pk_parse_public_key(&pub_key, mid_public_key, key_len + 1)) != 0) {
		ESP_LOGE(TAG, "mbedtls_pk_parse_public_key returned -0x%04x", (unsigned int) -ret);
		return -1;
	}

	if ((ret = mbedtls_pk_check_pair(&pub_key, &key, mbedtls_ctr_drbg_random, &ctr_drbg)) != 0) {
		ESP_LOGE(TAG, "mbedtls_pk_check_pair returned -0x%04x", (unsigned int) -ret);
		return -1;
	}

	mbedtls_pk_free(&pub_key);

	return 0;
}

static int mid_keypair_generate(void) {
	mbedtls_pk_init(&key);

	int ret;
	if ((ret = mbedtls_pk_setup(&key, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY))) != 0) {
		ESP_LOGE(TAG, "mbedtls_pk_setup returned -0x%04x", (unsigned int) -ret);
		return -1;
	}

	if ((ret = mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP384R1, mbedtls_pk_ec(key), mbedtls_ctr_drbg_random, &ctx->ctr_drbg)) != 0) {
		ESP_LOGE(TAG, "mbedtls_ecp_gen_key returned -0x%04x", (unsigned int) -ret);
		return -1;
	}

	if ((ret = mbedtls_pk_write_key_pem(&key, mid_private_key, sizeof (mid_private_key))) != 0) {
		ESP_LOGE(TAG, "mbedtls_pk_write_key_pem returned -0x%04x", (unsigned int) -ret);
		return -1;
	}

	if ((ret = mbedtls_pk_write_pubkey_pem(&key, mid_public_key, sizeof (mid_public_key))) != 0) {
		ESP_LOGE(TAG, "mbedtls_pk_write_pubkey_pem returned -0x%04x", (unsigned int) -ret);
		return -1;
	}

	return 0;

*/
	/*
	mbedtls_ecdsa_context ecdsa_ctx;
	mbedtls_ecdsa_init(&ecdsa_ctx);

	if ((ret = mbedtls_ecdsa_from_keypair(&ecdsa_ctx, mbedtls_pk_ec(key))) != 0) {
		ESP_LOGE(TAG, "mbedtls_ecdsa_from_keypair returned -0x%04x", (unsigned int) -ret);
		return -1;
	}

	unsigned char hash[32];

	unsigned char *msg = (unsigned char *)"example";
	size_t msg_len = strlen((char *)msg);

    if ((ret = mbedtls_sha256(msg, msg_len, hash, 0)) != 0) {
        ESP_LOGE(TAG, "mbedtls_sha256 returned -0x%04x\n", (unsigned int) -ret);
		return -1;
    }

	unsigned char sig[MBEDTLS_ECDSA_MAX_LEN];
	size_t sig_len;

	if ((ret = mbedtls_ecdsa_write_signature(&ecdsa_ctx, MBEDTLS_MD_SHA256, hash, sizeof (hash), sig, sizeof (sig), &sig_len,
					mbedtls_ctr_drbg_random, &ctr_drbg)) != 0) {
		ESP_LOGE(TAG, "mbedtls_ecdsa_write_signature returned -0x%04x", (unsigned int) -ret);
		return -1;
	}


	size_t base64_len;
	char *base64 = base64_encode(sig, sig_len, &base64_len);

	ESP_LOGI(TAG, "Signature: %s", base64);

	free(base64);

	// Test verification

	if ((ret = mbedtls_ecdsa_read_signature(&ecdsa_ctx, hash, sizeof (hash), sig, sig_len)) != 0) {
		ESP_LOGE(TAG, "mbedtls_ecdsa_read_signature returned -0x%04x", (unsigned int) -ret);
		return -1;
	}

	mbedtls_ecdsa_free(&ecdsa_ctx);

    mbedtls_pk_free(&key);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);

	return 0;
}
	*/


