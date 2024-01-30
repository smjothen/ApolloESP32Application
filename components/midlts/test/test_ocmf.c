#include <time.h>
#include <limits.h>

#include "unity.h"

#include "esp_log.h"
#include "mid_lts.h"
#include "mid_ocmf.h"
#include "mid_lts_test.h"

static const char *TAG = "MIDTEST";

static const mid_session_version_fw_t fw = { 2, 0, 4, 201 };
static const mid_session_version_lr_t lr = { 1, 2, 3 };

TEST_CASE("Test OCMF signed energy - No time flag", "[ocmf][allowleak]") {
	mid_session_meter_value_t meter_value = { .fw = fw, .lr = lr, .time = 0, .meter = 0, .flag = MID_SESSION_METER_VALUE_READING_FLAG_TARIFF };

	char buf[512];
	TEST_ASSERT_EQUAL_INT(-1, midocmf_fiscal_from_meter_value(buf, sizeof (buf), "ZAP000001", &meter_value, NULL));
}

TEST_CASE("Test OCMF signed energy - No reading flag", "[ocmf][allowleak]") {
	mid_session_meter_value_t meter_value = { .fw = fw, .lr = lr, .time = 0, .meter = 0, .flag = MID_SESSION_METER_VALUE_FLAG_TIME_SYNCHRONIZED };

	char buf[512];
	TEST_ASSERT_EQUAL_INT(-1, midocmf_fiscal_from_meter_value(buf, sizeof (buf), "ZAP000001", &meter_value, NULL));
}

TEST_CASE("Test OCMF signed energy - Has time flag and reading flag", "[ocmf][allowleak]") {
	mid_session_meter_value_t meter_value = { .fw = fw, .lr = lr, .time = 0, .meter = 0, .flag = MID_SESSION_METER_VALUE_FLAG_TIME_SYNCHRONIZED | MID_SESSION_METER_VALUE_READING_FLAG_TARIFF };
	char buf[512];
	TEST_ASSERT_EQUAL_INT(0, midocmf_fiscal_from_meter_value(buf, sizeof (buf), "ZAP000001", &meter_value, NULL));
	TEST_ASSERT_EQUAL_STRING("OCMF|{\"FV\":\"1.0\",\"GI\":\"Zaptec Go+\",\"GS\":\"ZAP000001\",\"GV\":\"2.0.4.201\",\"MF\":\"v1.2.3\",\"PG\":\"F1\",\"RD\":[{\"TM\":\"1970-01-01T00:00:00,000+00:00 S\",\"RV\":0,\"RI\":\"1-0:1.8.0\",\"RU\":\"kWh\",\"RT\":\"AC\",\"ST\":\"G\"}]}", buf);
}

TEST_CASE("Test OCMF signed energy only for tariff changes", "[ocmf]") {
	mid_session_meter_value_t meter_value = { .fw = fw, .lr = lr, .time = 0, .meter = 0, .flag = 0 };

	char buf[512];

	// Not a tariff change (flag = 0)
	TEST_ASSERT_EQUAL_INT(-1, midocmf_fiscal_from_meter_value(buf, sizeof (buf), "ZAP000001", &meter_value, NULL));
}

TEST_CASE("Test OCMF signed energy - Unknown", "[ocmf]") {
	mid_session_meter_value_t meter_value = { .fw = fw, .lr = lr, .time = 0, .meter = 0, .flag = MID_SESSION_METER_VALUE_READING_FLAG_TARIFF | MID_SESSION_METER_VALUE_FLAG_TIME_UNKNOWN };

	char buf[512];
	TEST_ASSERT_EQUAL_INT(0, midocmf_fiscal_from_meter_value(buf, sizeof (buf), "ZAP000001", &meter_value, NULL));


	TEST_ASSERT_EQUAL_STRING("OCMF|{\"FV\":\"1.0\",\"GI\":\"Zaptec Go+\",\"GS\":\"ZAP000001\",\"GV\":\"2.0.4.201\",\"MF\":\"v1.2.3\",\"PG\":\"F1\",\"RD\":[{\"TM\":\"1970-01-01T00:00:00,000+00:00 U\",\"RV\":0,\"RI\":\"1-0:1.8.0\",\"RU\":\"kWh\",\"RT\":\"AC\",\"ST\":\"G\"}]}", buf);
}

TEST_CASE("Test OCMF signed energy - Sync", "[ocmf]") {
	mid_session_meter_value_t meter_value = { .fw = fw, .lr = lr, .time = 0, .meter = 0, .flag = MID_SESSION_METER_VALUE_READING_FLAG_TARIFF | MID_SESSION_METER_VALUE_FLAG_TIME_SYNCHRONIZED };
	char buf[512];
	TEST_ASSERT_EQUAL_INT(0, midocmf_fiscal_from_meter_value(buf, sizeof (buf), "ZAP000001", &meter_value, NULL));
	TEST_ASSERT_EQUAL_STRING("OCMF|{\"FV\":\"1.0\",\"GI\":\"Zaptec Go+\",\"GS\":\"ZAP000001\",\"GV\":\"2.0.4.201\",\"MF\":\"v1.2.3\",\"PG\":\"F1\",\"RD\":[{\"TM\":\"1970-01-01T00:00:00,000+00:00 S\",\"RV\":0,\"RI\":\"1-0:1.8.0\",\"RU\":\"kWh\",\"RT\":\"AC\",\"ST\":\"G\"}]}", buf);
}

TEST_CASE("Test OCMF signed energy - Relative", "[ocmf]") {
	mid_session_meter_value_t meter_value = { .fw = fw, .lr = lr, .time = 0, .meter = 0, .flag = MID_SESSION_METER_VALUE_READING_FLAG_TARIFF | MID_SESSION_METER_VALUE_FLAG_TIME_RELATIVE };
	char buf[512];
	TEST_ASSERT_EQUAL_INT(0, midocmf_fiscal_from_meter_value(buf, sizeof (buf), "ZAP000001", &meter_value, NULL));
	TEST_ASSERT_EQUAL_STRING("OCMF|{\"FV\":\"1.0\",\"GI\":\"Zaptec Go+\",\"GS\":\"ZAP000001\",\"GV\":\"2.0.4.201\",\"MF\":\"v1.2.3\",\"PG\":\"F1\",\"RD\":[{\"TM\":\"1970-01-01T00:00:00,000+00:00 R\",\"RV\":0,\"RI\":\"1-0:1.8.0\",\"RU\":\"kWh\",\"RT\":\"AC\",\"ST\":\"G\"}]}", buf);
}

TEST_CASE("Test OCMF signed energy - Informative", "[ocmf]") {
	mid_session_meter_value_t meter_value = { .fw = fw, .lr = lr, .time = 0, .meter = 0, .flag = MID_SESSION_METER_VALUE_READING_FLAG_TARIFF | MID_SESSION_METER_VALUE_FLAG_TIME_INFORMATIVE };
	char buf[512];
	TEST_ASSERT_EQUAL_INT(0, midocmf_fiscal_from_meter_value(buf, sizeof (buf), "ZAP000001", &meter_value, NULL));
	TEST_ASSERT_EQUAL_STRING("OCMF|{\"FV\":\"1.0\",\"GI\":\"Zaptec Go+\",\"GS\":\"ZAP000001\",\"GV\":\"2.0.4.201\",\"MF\":\"v1.2.3\",\"PG\":\"F1\",\"RD\":[{\"TM\":\"1970-01-01T00:00:00,000+00:00 I\",\"RV\":0,\"RI\":\"1-0:1.8.0\",\"RU\":\"kWh\",\"RT\":\"AC\",\"ST\":\"G\"}]}", buf);
}

TEST_CASE("Test OCMF signed energy with event log", "[ocmf]") {
	mid_session_meter_value_t meter_value = { .fw = fw, .lr = lr, .time = 0, .meter = 0, .flag = MID_SESSION_METER_VALUE_READING_FLAG_TARIFF | MID_SESSION_METER_VALUE_FLAG_TIME_INFORMATIVE };

	char buf[512];

	mid_event_log_t log;
	TEST_ASSERT_EQUAL_INT(0, mid_event_log_init(&log));

	// Empty log
	TEST_ASSERT_EQUAL_INT(0, midocmf_fiscal_from_meter_value(buf, sizeof (buf), "ZAP000001", &meter_value, &log));
	ESP_LOGI(TAG, "%s", buf);
	TEST_ASSERT_EQUAL_STRING("OCMF|{\"FV\":\"1.0\",\"GI\":\"Zaptec Go+\",\"GS\":\"ZAP000001\",\"GV\":\"2.0.4.201\",\"MF\":\"v1.2.3\",\"PG\":\"F1\",\"RD\":[{\"TM\":\"1970-01-01T00:00:00,000+00:00 I\",\"RV\":0,\"RI\":\"1-0:1.8.0\",\"RU\":\"kWh\",\"RT\":\"AC\",\"ST\":\"G\"}],\"ZE\":[]}", buf);

	mid_event_log_entry_t entry = {.type = MID_EVENT_LOG_TYPE_INIT, 0, 0};
	TEST_ASSERT_EQUAL_INT(0, mid_event_log_add(&log, &entry));

	// 0 Init
	TEST_ASSERT_EQUAL_INT(0, midocmf_fiscal_from_meter_value(buf, sizeof (buf), "ZAP000001", &meter_value, &log));
	ESP_LOGI(TAG, "%s", buf);
	TEST_ASSERT_EQUAL_STRING("OCMF|{\"FV\":\"1.0\",\"GI\":\"Zaptec Go+\",\"GS\":\"ZAP000001\",\"GV\":\"2.0.4.201\",\"MF\":\"v1.2.3\",\"PG\":\"F1\",\"RD\":[{\"TM\":\"1970-01-01T00:00:00,000+00:00 I\",\"RV\":0,\"RI\":\"1-0:1.8.0\",\"RU\":\"kWh\",\"RT\":\"AC\",\"ST\":\"G\"}],\"ZE\":[{\"ES\":0,\"ET\":\"INIT\",\"EC\":0}]}", buf);


	// v1.0.6
	mid_event_log_entry_t entry2 = {.type = MID_EVENT_LOG_TYPE_FAIL, .seq = 1, .data = (0 << 8) | 6};
	TEST_ASSERT_EQUAL_INT(0, mid_event_log_add(&log, &entry2));

	TEST_ASSERT_EQUAL_INT(0, midocmf_fiscal_from_meter_value(buf, sizeof (buf), "ZAP000001", &meter_value, &log));
	ESP_LOGI(TAG, "%s", buf);
	TEST_ASSERT_EQUAL_STRING("OCMF|{\"FV\":\"1.0\",\"GI\":\"Zaptec Go+\",\"GS\":\"ZAP000001\",\"GV\":\"2.0.4.201\",\"MF\":\"v1.2.3\",\"PG\":\"F1\",\"RD\":[{\"TM\":\"1970-01-01T00:00:00,000+00:00 I\",\"RV\":0,\"RI\":\"1-0:1.8.0\",\"RU\":\"kWh\",\"RT\":\"AC\",\"ST\":\"G\"}],\"ZE\":[{\"ES\":0,\"ET\":\"INIT\",\"EC\":0},{\"ES\":1,\"ET\":\"FAIL\",\"EV\":\"v1.0.6\"}]}", buf);

	// v1.0.6
	mid_event_log_entry_t entry3 = {.type = MID_EVENT_LOG_TYPE_START, .seq = 2, .data = (0 << 8) | 6};
	TEST_ASSERT_EQUAL_INT(0, mid_event_log_add(&log, &entry3));

	TEST_ASSERT_EQUAL_INT(0, midocmf_fiscal_from_meter_value(buf, sizeof (buf), "ZAP000001", &meter_value, &log));
	ESP_LOGI(TAG, "%s", buf);
	TEST_ASSERT_EQUAL_STRING("OCMF|{\"FV\":\"1.0\",\"GI\":\"Zaptec Go+\",\"GS\":\"ZAP000001\",\"GV\":\"2.0.4.201\",\"MF\":\"v1.2.3\",\"PG\":\"F1\",\"RD\":[{\"TM\":\"1970-01-01T00:00:00,000+00:00 I\",\"RV\":0,\"RI\":\"1-0:1.8.0\",\"RU\":\"kWh\",\"RT\":\"AC\",\"ST\":\"G\"}],\"ZE\":[{\"ES\":0,\"ET\":\"INIT\",\"EC\":0},{\"ES\":1,\"ET\":\"FAIL\",\"EV\":\"v1.0.6\"},{\"ES\":2,\"ET\":\"START\",\"EV\":\"v1.0.6\"}]}", buf);

	// v1.3.6
	mid_event_log_entry_t entry4 = {.type = MID_EVENT_LOG_TYPE_SUCCESS, .seq = 3, .data = (3 << 8) | 6};
	TEST_ASSERT_EQUAL_INT(0, mid_event_log_add(&log, &entry4));

	TEST_ASSERT_EQUAL_INT(0, midocmf_fiscal_from_meter_value(buf, sizeof (buf), "ZAP000001", &meter_value, &log));
	ESP_LOGI(TAG, "%s", buf);
	TEST_ASSERT_EQUAL_STRING("OCMF|{\"FV\":\"1.0\",\"GI\":\"Zaptec Go+\",\"GS\":\"ZAP000001\",\"GV\":\"2.0.4.201\",\"MF\":\"v1.2.3\",\"PG\":\"F1\",\"RD\":[{\"TM\":\"1970-01-01T00:00:00,000+00:00 I\",\"RV\":0,\"RI\":\"1-0:1.8.0\",\"RU\":\"kWh\",\"RT\":\"AC\",\"ST\":\"G\"}],\"ZE\":[{\"ES\":0,\"ET\":\"INIT\",\"EC\":0},{\"ES\":1,\"ET\":\"FAIL\",\"EV\":\"v1.0.6\"},{\"ES\":2,\"ET\":\"START\",\"EV\":\"v1.0.6\"},{\"ES\":3,\"ET\":\"SUCCESS\",\"EV\":\"v1.3.6\"}]}", buf);

	// Erase counter
	mid_event_log_entry_t entry5 = {.type = MID_EVENT_LOG_TYPE_ERASE, .seq = 4, .data = 1};
	TEST_ASSERT_EQUAL_INT(0, mid_event_log_add(&log, &entry5));

	TEST_ASSERT_EQUAL_INT(0, midocmf_fiscal_from_meter_value(buf, sizeof (buf), "ZAP000001", &meter_value, &log));
	ESP_LOGI(TAG, "%s", buf);
	TEST_ASSERT_EQUAL_STRING("OCMF|{\"FV\":\"1.0\",\"GI\":\"Zaptec Go+\",\"GS\":\"ZAP000001\",\"GV\":\"2.0.4.201\",\"MF\":\"v1.2.3\",\"PG\":\"F1\",\"RD\":[{\"TM\":\"1970-01-01T00:00:00,000+00:00 I\",\"RV\":0,\"RI\":\"1-0:1.8.0\",\"RU\":\"kWh\",\"RT\":\"AC\",\"ST\":\"G\"}],\"ZE\":[{\"ES\":0,\"ET\":\"INIT\",\"EC\":0},{\"ES\":1,\"ET\":\"FAIL\",\"EV\":\"v1.0.6\"},{\"ES\":2,\"ET\":\"START\",\"EV\":\"v1.0.6\"},{\"ES\":3,\"ET\":\"SUCCESS\",\"EV\":\"v1.3.6\"},{\"ES\":4,\"ET\":\"ERASE\",\"EC\":1}]}", buf);

	mid_event_log_free(&log);
}

static char key_priv[512] = {0};
static char key_pub[512] = {0};

const char *ocmf_pub = "-----BEGIN PUBLIC KEY-----\n"
"MHYwEAYHKoZIzj0CAQYFK4EEACIDYgAEj6hkVvHVvhM8mFm1/CkkDPTMTf0nMikK\n"
"Pw57yHHVO5fJLTTfZdN78XXanzdAe6JK3KqIQj/QXV5HV1XOdZ1Dy0AXykw/h8VZ\n"
"f+B8Mw3BhRcKx27PBTBfe8y7HITM3MzS\n"
"-----END PUBLIC KEY-----\n";

const char *ocmf_prv = "-----BEGIN EC PRIVATE KEY-----\n"
"MIGkAgEBBDDn4fPF8Q992eRJY39nh7Gi8n7dq3hLnv8pQQaUNdI0OsPGiJFC8IEG\n"
"WzcTMpqyPemgBwYFK4EEACKhZANiAASPqGRW8dW+EzyYWbX8KSQM9MxN/ScyKQo/\n"
"DnvIcdU7l8ktNN9l03vxddqfN0B7okrcqohCP9BdXkdXVc51nUPLQBfKTD+HxVl/\n"
"4HwzDcGFFwrHbs8FMF97zLschMzczNI=\n"
"-----END EC PRIVATE KEY-----\n";

TEST_CASE("Test OCMF signed energy - with signature", "[ocmf]") {
	mid_session_meter_value_t meter_value = { .fw = fw, .lr = lr, .time = 0, .meter = 0, .flag = MID_SESSION_METER_VALUE_READING_FLAG_TARIFF | MID_SESSION_METER_VALUE_FLAG_TIME_SYNCHRONIZED };

	snprintf(key_priv, sizeof (key_priv), "%s", ocmf_prv);
	snprintf(key_pub, sizeof (key_pub), "%s", ocmf_pub);

	mid_sign_ctx_t *ctx = mid_sign_ctx_get_global();
	TEST_ASSERT_EQUAL_INT(0, mid_sign_ctx_init(ctx, key_priv, sizeof (key_priv), key_pub, sizeof (key_pub)));

	char buf[512];

	TEST_ASSERT_EQUAL_INT(0, midocmf_fiscal_from_meter_value_signed(buf, sizeof (buf), "ZAP000001", &meter_value, NULL, ctx));

	ESP_LOGI(TAG, "%s", key_pub);
	ESP_LOGI(TAG, "%s", buf);

	TEST_ASSERT_EQUAL_STRING("OCMF|{\"FV\":\"1.0\",\"GI\":\"Zaptec Go+\",\"GS\":\"ZAP000001\",\"GV\":\"2.0.4.201\",\"MF\":\"v1.2.3\",\"PG\":\"F1\",\"RD\":[{\"TM\":\"1970-01-01T00:00:00,000+00:00 S\",\"RV\":0,\"RI\":\"1-0:1.8.0\",\"RU\":\"kWh\",\"RT\":\"AC\",\"ST\":\"G\"}]}|{\"SA\":\"ECDSA-secp384r1-SHA256\",\"SE\":\"base64\",\"SD\":\"MGQCMD12Gm6LBicNABDKtgHbxwa7moA2whL74yjf7gt57X7845GJfvlQncsXhkuNVCEqFAIwKa26PCpQkKAA7HZr4Qm4WW+N562AGVBAmvr4tH9uSwQT5T/Y9bI9nf8JGZMVUKBz\"}", buf);

	mid_sign_ctx_free(ctx);
}

TEST_CASE("Test packed time - milliseconds", "[ocmf]") {
	uint64_t time = 1706600762ULL * 1000 + 999;
	struct timespec ts = MID_TIME_TO_TS(time);
	uint64_t time2 = MID_TS_TO_TIME(ts);
	TEST_ASSERT_EQUAL_INT(time, time2);
}

TEST_CASE("Test OCMF signed energy - milliseconds", "[ocmf]") {
	uint64_t time = 1706600762ULL * 1000 + 999;
	mid_session_meter_value_t meter_value = { .fw = fw, .lr = lr, .time = time, .meter = 0, .flag = MID_SESSION_METER_VALUE_READING_FLAG_TARIFF | MID_SESSION_METER_VALUE_FLAG_TIME_SYNCHRONIZED };

	snprintf(key_priv, sizeof (key_priv), "%s", ocmf_prv);
	snprintf(key_pub, sizeof (key_pub), "%s", ocmf_pub);

	mid_sign_ctx_t *ctx = mid_sign_ctx_get_global();
	TEST_ASSERT_EQUAL_INT(0, mid_sign_ctx_init(ctx, key_priv, sizeof (key_priv), key_pub, sizeof (key_pub)));

	char buf[512];

	TEST_ASSERT_EQUAL_INT(0, midocmf_fiscal_from_meter_value_signed(buf, sizeof (buf), "ZAP000001", &meter_value, NULL, ctx));

	ESP_LOGI(TAG, "%s", key_pub);
	ESP_LOGI(TAG, "%s", buf);

	TEST_ASSERT_EQUAL_STRING("OCMF|{\"FV\":\"1.0\",\"GI\":\"Zaptec Go+\",\"GS\":\"ZAP000001\",\"GV\":\"2.0.4.201\",\"MF\":\"v1.2.3\",\"PG\":\"F1\",\"RD\":[{\"TM\":\"2024-01-30T07:46:02,999+00:00 S\",\"RV\":0,\"RI\":\"1-0:1.8.0\",\"RU\":\"kWh\",\"RT\":\"AC\",\"ST\":\"G\"}]}|{\"SA\":\"ECDSA-secp384r1-SHA256\",\"SE\":\"base64\",\"SD\":\"MGYCMQDvTdoosfMWqOvt2PN5fF/T7DigZ38Ik7s3TD1TokV21l3LWB5YY4xRFEAoB7iwCLUCMQCY7Y6P71fsK8XWJnDktHnRCOtN/Aw3SxD4drdHtKX+NeTiORcXg0wo4Swvhz7Qre8=\"}", buf);

	mid_sign_ctx_free(ctx);
}
