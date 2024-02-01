#include <time.h>
#include <string.h>
#include <limits.h>

#include "unity.h"
#include "esp_log.h"
#include "unity.h"
#include "memory_checks.h"
#include "ccomp_timer.h"
#include "unity_test_utils_memory.h"
#include "esp_timer.h"

#include "mid_lts.h"
#include "mid_sign.h"

static const char *TAG = "MIDSIGN";

const char *openssl_pub = "-----BEGIN PUBLIC KEY-----\n"
"MHYwEAYHKoZIzj0CAQYFK4EEACIDYgAEpxzQTKmYsqK5+eAJ37bLWeQPVYLhF9jj\n"
"Jx/AQFQLmod4McKvcuCuq0T1WRCO50S6l2IMBvi96XMFRIQ+4+wAZWUX2l7CWzaK\n"
"w6s46MGbT5Y6D9WjqOFPMOrKcKJ2BcB4\n"
"-----END PUBLIC KEY-----\n";

const char *openssl_prv = "-----BEGIN EC PRIVATE KEY-----\n"
"MIGkAgEBBDAHX+5TTVVD6cTuZR5EbJA+qvBemJvrnl6fzvelDGh6vCupn8iJoxY/\n"
"cbYk9LFL3CagBwYFK4EEACKhZANiAASnHNBMqZiyorn54AnftstZ5A9VguEX2OMn\n"
"H8BAVAuah3gxwq9y4K6rRPVZEI7nRLqXYgwG+L3pcwVEhD7j7ABlZRfaXsJbNorD\n"
"qzjowZtPljoP1aOo4U8w6spwonYFwHg=\n"
"-----END EC PRIVATE KEY-----\n";

const char *load_pub = "-----BEGIN PUBLIC KEY-----\n"
"MHYwEAYHKoZIzj0CAQYFK4EEACIDYgAEj6hkVvHVvhM8mFm1/CkkDPTMTf0nMikK\n"
"Pw57yHHVO5fJLTTfZdN78XXanzdAe6JK3KqIQj/QXV5HV1XOdZ1Dy0AXykw/h8VZ\n"
"f+B8Mw3BhRcKx27PBTBfe8y7HITM3MzS\n"
"-----END PUBLIC KEY-----\n";

const char *load_prv = "-----BEGIN EC PRIVATE KEY-----\n"
"MIGkAgEBBDDn4fPF8Q992eRJY39nh7Gi8n7dq3hLnv8pQQaUNdI0OsPGiJFC8IEG\n"
"WzcTMpqyPemgBwYFK4EEACKhZANiAASPqGRW8dW+EzyYWbX8KSQM9MxN/ScyKQo/\n"
"DnvIcdU7l8ktNN9l03vxddqfN0B7okrcqohCP9BdXkdXVc51nUPLQBfKTD+HxVl/\n"
"4HwzDcGFFwrHbs8FMF97zLschMzczNI=\n"
"-----END EC PRIVATE KEY-----\n";

static char key_prv[512] = {0};
static char key_pub[512] = {0};

static char sig_buf[512];
static char buf[256];

mid_sign_ctx_t ctx_main = {0};

TEST_CASE("Test generation of keys", "[midsign][allowleak]") {
	TEST_ASSERT(mid_sign_ctx_generate(key_prv, sizeof (key_prv), key_pub, sizeof (key_pub)) == 0);

	TEST_ASSERT(strstr(key_pub, "BEGIN PUBLIC KEY") != NULL);
	TEST_ASSERT(strstr(key_pub, "END PUBLIC KEY") != NULL);
	TEST_ASSERT(strstr(key_prv, "BEGIN EC PRIVATE KEY") != NULL);
	TEST_ASSERT(strstr(key_prv, "END EC PRIVATE KEY") != NULL);

	TEST_ASSERT(mid_sign_ctx_init(&ctx_main, key_prv, key_pub) == 0);
	TEST_ASSERT_EQUAL_INT(MID_SIGN_FLAG_INITIALIZED, ctx_main.flag & MID_SIGN_FLAG_INITIALIZED);

	size_t total = 0;
	for (size_t j = 0; j < 16; j++) {
		for (size_t i = 0; i < sizeof (buf); i++) {
			buf[i] = esp_random();
		}
		uint64_t start = esp_timer_get_time();
		size_t sig_len = sizeof (sig_buf);
		int ret = mid_sign_ctx_sign(&ctx_main, buf, sizeof (buf), sig_buf, &sig_len);
		uint64_t end = esp_timer_get_time();
		ESP_LOGI(TAG, "Signed with %zu byte signature", sig_len);
		TEST_ASSERT_EQUAL_INT(ret, 0);
		total += end - start;
	}

	ESP_LOGI(TAG, "Signing 16 times = %fms, average %fms", total / 1000.0, total / (16.0 * 1000.0));
}

TEST_CASE("Test loading of keys", "[midsign][allowleak]") {
	mid_sign_ctx_t ctx = {0};
	TEST_ASSERT(mid_sign_ctx_init(&ctx, (char *)load_prv, (char *)load_pub) == 0);
	TEST_ASSERT_EQUAL_INT(MID_SIGN_FLAG_INITIALIZED, ctx.flag & MID_SIGN_FLAG_INITIALIZED);
	TEST_ASSERT(mid_sign_ctx_free(&ctx) == 0);
}

TEST_CASE("Test signing", "[midsign][allowleak]") {
	mid_sign_ctx_t ctx = {0};
	TEST_ASSERT(mid_sign_ctx_init(&ctx, (char *)load_prv, (char *)load_pub) == 0);
	TEST_ASSERT_EQUAL_INT(MID_SIGN_FLAG_INITIALIZED, ctx.flag & MID_SIGN_FLAG_INITIALIZED);

	size_t sig_len = sizeof (sig_buf);
	char *text = (char *)"This is a test!";
	TEST_ASSERT(mid_sign_ctx_sign(&ctx, text, strlen(text), sig_buf, &sig_len) == 0);
	TEST_ASSERT(mid_sign_ctx_verify(&ctx, text, strlen(text), sig_buf, sig_len) == 0);

	TEST_ASSERT(mid_sign_ctx_free(&ctx) == 0);
}

TEST_CASE("Test OpenSSL verify", "[midsign][allowleak]") {
	mid_sign_ctx_t ctx = {0};
	TEST_ASSERT(mid_sign_ctx_init(&ctx, (char *)openssl_prv, (char *)openssl_pub) == 0);
	TEST_ASSERT_EQUAL_INT(MID_SIGN_FLAG_INITIALIZED, ctx.flag & MID_SIGN_FLAG_INITIALIZED);

	char *text = (char *)"hello world\n";
	char *sig = (char *)"MGUCMDgo5Wio/A8GnVldNEpFvs03fcbLOnlv4hXF4lIpEQQS9d5XYZ/e03JXJoYxibT0QAIxAKqMRheucRQT2BSxJFkT8ZhWngxn1ZFgNcabk6UbBXRETG5xRaSdLn/iarPJjU1PSg==";

	TEST_ASSERT(mid_sign_ctx_verify(&ctx, text, strlen(text), sig, strlen(sig)) == 0);
	TEST_ASSERT(mid_sign_ctx_free(&ctx) == 0);
}
