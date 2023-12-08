#ifndef __MID_SIGN_H__
#define __MID_SIGN_H__

#include "mbedtls/ecdsa.h"
#include "mbedtls/pk.h"
#include "mbedtls/sha256.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"

#include "wpa_supplicant/base64.h"

#define MID_SIGN_FLAG_INITIALIZED 1
#define MID_SIGN_FLAG_VERIFIED 2

typedef struct {
	int flag;
	mbedtls_pk_context key;
	mbedtls_entropy_context entropy;
	mbedtls_ctr_drbg_context ctr_drbg;
} MIDSignCtx;

MIDSignCtx *mid_sign_ctx_get_global(void);
int mid_sign_ctx_init(MIDSignCtx *ctx, char *prv_pem, char *pub_pem);
int mid_sign_ctx_verify_public_key(MIDSignCtx *ctx, char *pub_pem);
int mid_sign_ctx_generate_key(MIDSignCtx *ctx);
int mid_sign_ctx_get_private_key(MIDSignCtx *ctx, char *buf, size_t buf_size);
int mid_sign_ctx_get_public_key(MIDSignCtx *ctx, char *buf, size_t buf_size);

#endif
