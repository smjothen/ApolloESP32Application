#ifndef __MID_SIGN_H__
#define __MID_SIGN_H__

#include "mbedtls/ecdsa.h"
#include "mbedtls/pk.h"
#include "mbedtls/sha256.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"

#define MID_SIGN_FLAG_INITIALIZED 1
#define MID_SIGN_FLAG_VERIFIED 2
#define MID_SIGN_FLAG_GENERATED 4

typedef struct {
	int flag;
	mbedtls_pk_context key;
	mbedtls_entropy_context entropy;
	mbedtls_ctr_drbg_context ctr_drbg;
	mbedtls_ecdsa_context ecdsa;
} MIDSignCtx;

MIDSignCtx *mid_sign_ctx_get_global(void);

int mid_sign_ctx_init(MIDSignCtx *ctx, char *prv_buf, size_t prv_size, char *pub_buf, size_t pub_size);
int mid_sign_ctx_get_public_key(MIDSignCtx *ctx, char *buf, size_t buf_size);

int mid_sign_ctx_sign(MIDSignCtx *ctx, char *str, size_t str_len, char *sig64, size_t *sig64_len);
int mid_sign_ctx_verify(MIDSignCtx *ctx, char *str, size_t str_len, char *sig64, size_t sig64_len);

#endif
