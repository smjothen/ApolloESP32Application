#include <time.h>
#include <limits.h>

#include "unity.h"
#include "esp_log.h"

#include "mid_lts.h"
#include "mid_ocmf.h"
#include "mid_lts_test.h"
#include "mid_active.h"

static const char *TAG = "MIDTEST";

static const mid_session_version_fw_t fw = { 2, 0, 4, 201 };
static const mid_session_version_lr_t lr = { 1, 2, 3 };

TEST_CASE("Test OCMF signed energy - No time flag", "[ocmf][allowleak]") {
	mid_session_meter_value_t meter_value = { .fw = fw, .lr = lr, .time = 0, .meter = 0, .flag = MID_SESSION_METER_VALUE_READING_FLAG_TARIFF };
	TEST_ASSERT_EQUAL(NULL, midocmf_signed_fiscal_from_meter_value(NULL, "ZAP000001", &meter_value, NULL));
}

TEST_CASE("Test OCMF signed energy - No reading flag", "[ocmf][allowleak]") {
	mid_session_meter_value_t meter_value = { .fw = fw, .lr = lr, .time = 0, .meter = 0, .flag = MID_SESSION_METER_VALUE_FLAG_TIME_SYNCHRONIZED };
	TEST_ASSERT_EQUAL(NULL, midocmf_signed_fiscal_from_meter_value(NULL, "ZAP000001", &meter_value, NULL));
}

TEST_CASE("Test OCMF signed energy - Has time flag and reading flag", "[ocmf][allowleak]") {
	mid_session_meter_value_t meter_value = { .fw = fw, .lr = lr, .time = 0, .meter = 0, .flag = MID_SESSION_METER_VALUE_FLAG_TIME_SYNCHRONIZED | MID_SESSION_METER_VALUE_READING_FLAG_TARIFF };
	const char *buf = midocmf_signed_fiscal_from_meter_value(NULL, "ZAP000001", &meter_value, NULL);
	TEST_ASSERT_EQUAL_STRING("OCMF|{\"FV\":\"1.0\",\"GI\":\"Zaptec Go+\",\"GS\":\"ZAP000001\",\"GV\":\"2.0.4.201\",\"MF\":\"v1.2.3\",\"PG\":\"F1\",\"RD\":[{\"TM\":\"1970-01-01T00:00:00,000+00:00 S\",\"RV\":0,\"RI\":\"1-0:1.8.0\",\"RU\":\"kWh\",\"RT\":\"AC\",\"ST\":\"G\"}]}", buf);
	free((char *)buf);
}

TEST_CASE("Test OCMF signed energy only for tariff changes", "[ocmf]") {
	mid_session_meter_value_t meter_value = { .fw = fw, .lr = lr, .time = 0, .meter = 0, .flag = 0 };
	// Not a tariff change (flag = 0)
	TEST_ASSERT_EQUAL(NULL, midocmf_signed_fiscal_from_meter_value(NULL, "ZAP000001", &meter_value, NULL));
}

TEST_CASE("Test OCMF signed energy - Unknown", "[ocmf]") {
	mid_session_meter_value_t meter_value = { .fw = fw, .lr = lr, .time = 0, .meter = 0, .flag = MID_SESSION_METER_VALUE_READING_FLAG_TARIFF | MID_SESSION_METER_VALUE_FLAG_TIME_UNKNOWN };
	const char * buf = midocmf_signed_fiscal_from_meter_value(NULL, "ZAP000001", &meter_value, NULL);
	TEST_ASSERT_EQUAL_STRING("OCMF|{\"FV\":\"1.0\",\"GI\":\"Zaptec Go+\",\"GS\":\"ZAP000001\",\"GV\":\"2.0.4.201\",\"MF\":\"v1.2.3\",\"PG\":\"F1\",\"RD\":[{\"TM\":\"1970-01-01T00:00:00,000+00:00 U\",\"RV\":0,\"RI\":\"1-0:1.8.0\",\"RU\":\"kWh\",\"RT\":\"AC\",\"ST\":\"G\"}]}", buf);
	free((char *)buf);
}

TEST_CASE("Test OCMF signed energy - Sync", "[ocmf]") {
	mid_session_meter_value_t meter_value = { .fw = fw, .lr = lr, .time = 0, .meter = 0, .flag = MID_SESSION_METER_VALUE_READING_FLAG_TARIFF | MID_SESSION_METER_VALUE_FLAG_TIME_SYNCHRONIZED };
	const char * buf = midocmf_signed_fiscal_from_meter_value(NULL, "ZAP000001", &meter_value, NULL);
	TEST_ASSERT_EQUAL_STRING("OCMF|{\"FV\":\"1.0\",\"GI\":\"Zaptec Go+\",\"GS\":\"ZAP000001\",\"GV\":\"2.0.4.201\",\"MF\":\"v1.2.3\",\"PG\":\"F1\",\"RD\":[{\"TM\":\"1970-01-01T00:00:00,000+00:00 S\",\"RV\":0,\"RI\":\"1-0:1.8.0\",\"RU\":\"kWh\",\"RT\":\"AC\",\"ST\":\"G\"}]}", buf);
	free((char *)buf);
}

TEST_CASE("Test OCMF signed energy - Relative", "[ocmf]") {
	mid_session_meter_value_t meter_value = { .fw = fw, .lr = lr, .time = 0, .meter = 0, .flag = MID_SESSION_METER_VALUE_READING_FLAG_TARIFF | MID_SESSION_METER_VALUE_FLAG_TIME_RELATIVE };
	const char * buf  = midocmf_signed_fiscal_from_meter_value(NULL, "ZAP000001", &meter_value, NULL);
	TEST_ASSERT_EQUAL_STRING("OCMF|{\"FV\":\"1.0\",\"GI\":\"Zaptec Go+\",\"GS\":\"ZAP000001\",\"GV\":\"2.0.4.201\",\"MF\":\"v1.2.3\",\"PG\":\"F1\",\"RD\":[{\"TM\":\"1970-01-01T00:00:00,000+00:00 R\",\"RV\":0,\"RI\":\"1-0:1.8.0\",\"RU\":\"kWh\",\"RT\":\"AC\",\"ST\":\"G\"}]}", buf);
	free((char *)buf);
}

TEST_CASE("Test OCMF signed energy - Informative", "[ocmf]") {
	mid_session_meter_value_t meter_value = { .fw = fw, .lr = lr, .time = 0, .meter = 0, .flag = MID_SESSION_METER_VALUE_READING_FLAG_TARIFF | MID_SESSION_METER_VALUE_FLAG_TIME_INFORMATIVE };
	const char * buf = midocmf_signed_fiscal_from_meter_value(NULL, "ZAP000001", &meter_value, NULL);
	TEST_ASSERT_EQUAL_STRING("OCMF|{\"FV\":\"1.0\",\"GI\":\"Zaptec Go+\",\"GS\":\"ZAP000001\",\"GV\":\"2.0.4.201\",\"MF\":\"v1.2.3\",\"PG\":\"F1\",\"RD\":[{\"TM\":\"1970-01-01T00:00:00,000+00:00 I\",\"RV\":0,\"RI\":\"1-0:1.8.0\",\"RU\":\"kWh\",\"RT\":\"AC\",\"ST\":\"G\"}]}", buf);
	free((char *)buf);
}

TEST_CASE("Test OCMF signed energy with event log", "[ocmf]") {
	mid_session_meter_value_t meter_value = { .fw = fw, .lr = lr, .time = 0, .meter = 0, .flag = MID_SESSION_METER_VALUE_READING_FLAG_TARIFF | MID_SESSION_METER_VALUE_FLAG_TIME_INFORMATIVE };

	mid_event_log_t log;
	TEST_ASSERT_EQUAL_INT(0, mid_event_log_init(&log));

	// Empty log
	const char * buf = midocmf_signed_fiscal_from_meter_value(NULL, "ZAP000001", &meter_value, &log);
	ESP_LOGI(TAG, "%s", buf);
	TEST_ASSERT_EQUAL_STRING("OCMF|{\"FV\":\"1.0\",\"GI\":\"Zaptec Go+\",\"GS\":\"ZAP000001\",\"GV\":\"2.0.4.201\",\"MF\":\"v1.2.3\",\"PG\":\"F1\",\"RD\":[{\"TM\":\"1970-01-01T00:00:00,000+00:00 I\",\"RV\":0,\"RI\":\"1-0:1.8.0\",\"RU\":\"kWh\",\"RT\":\"AC\",\"ST\":\"G\"}],\"ZE\":[]}", buf);
	free((char *)buf);

	mid_event_log_entry_t entry = {.type = MID_EVENT_LOG_TYPE_INIT, 0, 0};
	TEST_ASSERT_EQUAL_INT(0, mid_event_log_add(&log, &entry));

	// 0 Init
	buf = midocmf_signed_fiscal_from_meter_value(NULL, "ZAP000001", &meter_value, &log);
	ESP_LOGI(TAG, "%s", buf);
	TEST_ASSERT_EQUAL_STRING("OCMF|{\"FV\":\"1.0\",\"GI\":\"Zaptec Go+\",\"GS\":\"ZAP000001\",\"GV\":\"2.0.4.201\",\"MF\":\"v1.2.3\",\"PG\":\"F1\",\"RD\":[{\"TM\":\"1970-01-01T00:00:00,000+00:00 I\",\"RV\":0,\"RI\":\"1-0:1.8.0\",\"RU\":\"kWh\",\"RT\":\"AC\",\"ST\":\"G\"}],\"ZE\":[{\"ES\":0,\"ET\":\"INIT\",\"EC\":0}]}", buf);
	free((char *)buf);

	// v1.0.6
	mid_event_log_entry_t entry2 = {.type = MID_EVENT_LOG_TYPE_FAIL, .seq = 1, .data = (0 << 8) | 6};
	TEST_ASSERT_EQUAL_INT(0, mid_event_log_add(&log, &entry2));

	buf = midocmf_signed_fiscal_from_meter_value(NULL, "ZAP000001", &meter_value, &log);
	ESP_LOGI(TAG, "%s", buf);
	TEST_ASSERT_EQUAL_STRING("OCMF|{\"FV\":\"1.0\",\"GI\":\"Zaptec Go+\",\"GS\":\"ZAP000001\",\"GV\":\"2.0.4.201\",\"MF\":\"v1.2.3\",\"PG\":\"F1\",\"RD\":[{\"TM\":\"1970-01-01T00:00:00,000+00:00 I\",\"RV\":0,\"RI\":\"1-0:1.8.0\",\"RU\":\"kWh\",\"RT\":\"AC\",\"ST\":\"G\"}],\"ZE\":[{\"ES\":0,\"ET\":\"INIT\",\"EC\":0},{\"ES\":1,\"ET\":\"FAIL\",\"EV\":\"v1.0.6\"}]}", buf);
	free((char *)buf);

	// v1.0.6
	mid_event_log_entry_t entry3 = {.type = MID_EVENT_LOG_TYPE_START, .seq = 2, .data = (0 << 8) | 6};
	TEST_ASSERT_EQUAL_INT(0, mid_event_log_add(&log, &entry3));

	buf = midocmf_signed_fiscal_from_meter_value(NULL, "ZAP000001", &meter_value, &log);
	ESP_LOGI(TAG, "%s", buf);
	TEST_ASSERT_EQUAL_STRING("OCMF|{\"FV\":\"1.0\",\"GI\":\"Zaptec Go+\",\"GS\":\"ZAP000001\",\"GV\":\"2.0.4.201\",\"MF\":\"v1.2.3\",\"PG\":\"F1\",\"RD\":[{\"TM\":\"1970-01-01T00:00:00,000+00:00 I\",\"RV\":0,\"RI\":\"1-0:1.8.0\",\"RU\":\"kWh\",\"RT\":\"AC\",\"ST\":\"G\"}],\"ZE\":[{\"ES\":0,\"ET\":\"INIT\",\"EC\":0},{\"ES\":1,\"ET\":\"FAIL\",\"EV\":\"v1.0.6\"},{\"ES\":2,\"ET\":\"START\",\"EV\":\"v1.0.6\"}]}", buf);
	free((char *)buf);

	// v1.3.6
	mid_event_log_entry_t entry4 = {.type = MID_EVENT_LOG_TYPE_SUCCESS, .seq = 3, .data = (3 << 8) | 6};
	TEST_ASSERT_EQUAL_INT(0, mid_event_log_add(&log, &entry4));

	buf = midocmf_signed_fiscal_from_meter_value(NULL, "ZAP000001", &meter_value, &log);
	ESP_LOGI(TAG, "%s", buf);
	TEST_ASSERT_EQUAL_STRING("OCMF|{\"FV\":\"1.0\",\"GI\":\"Zaptec Go+\",\"GS\":\"ZAP000001\",\"GV\":\"2.0.4.201\",\"MF\":\"v1.2.3\",\"PG\":\"F1\",\"RD\":[{\"TM\":\"1970-01-01T00:00:00,000+00:00 I\",\"RV\":0,\"RI\":\"1-0:1.8.0\",\"RU\":\"kWh\",\"RT\":\"AC\",\"ST\":\"G\"}],\"ZE\":[{\"ES\":0,\"ET\":\"INIT\",\"EC\":0},{\"ES\":1,\"ET\":\"FAIL\",\"EV\":\"v1.0.6\"},{\"ES\":2,\"ET\":\"START\",\"EV\":\"v1.0.6\"},{\"ES\":3,\"ET\":\"SUCCESS\",\"EV\":\"v1.3.6\"}]}", buf);
	free((char *)buf);

	// Erase counter
	mid_event_log_entry_t entry5 = {.type = MID_EVENT_LOG_TYPE_ERASE, .seq = 4, .data = 1};
	TEST_ASSERT_EQUAL_INT(0, mid_event_log_add(&log, &entry5));

	buf = midocmf_signed_fiscal_from_meter_value(NULL, "ZAP000001", &meter_value, &log);
	ESP_LOGI(TAG, "%s", buf);
	TEST_ASSERT_EQUAL_STRING("OCMF|{\"FV\":\"1.0\",\"GI\":\"Zaptec Go+\",\"GS\":\"ZAP000001\",\"GV\":\"2.0.4.201\",\"MF\":\"v1.2.3\",\"PG\":\"F1\",\"RD\":[{\"TM\":\"1970-01-01T00:00:00,000+00:00 I\",\"RV\":0,\"RI\":\"1-0:1.8.0\",\"RU\":\"kWh\",\"RT\":\"AC\",\"ST\":\"G\"}],\"ZE\":[{\"ES\":0,\"ET\":\"INIT\",\"EC\":0},{\"ES\":1,\"ET\":\"FAIL\",\"EV\":\"v1.0.6\"},{\"ES\":2,\"ET\":\"START\",\"EV\":\"v1.0.6\"},{\"ES\":3,\"ET\":\"SUCCESS\",\"EV\":\"v1.3.6\"},{\"ES\":4,\"ET\":\"ERASE\",\"EC\":1}]}", buf);
	free((char *)buf);

	mid_event_log_free(&log);
}

TEST_CASE("Test OCMF signed energy - with signature", "[ocmf]") {
	mid_session_meter_value_t meter_value = { .fw = fw, .lr = lr, .time = 0, .meter = 0, .flag = MID_SESSION_METER_VALUE_READING_FLAG_TARIFF | MID_SESSION_METER_VALUE_FLAG_TIME_SYNCHRONIZED };

	mid_sign_ctx_t *ctx = mid_sign_ctx_get_global();
	const char * buf = midocmf_signed_fiscal_from_meter_value(ctx, "ZAP000001", &meter_value, NULL);
	ESP_LOGI(TAG, "%s", buf);

	TEST_ASSERT_EQUAL_STRING("OCMF|{\"FV\":\"1.0\",\"GI\":\"Zaptec Go+\",\"GS\":\"ZAP000001\",\"GV\":\"2.0.4.201\",\"MF\":\"v1.2.3\",\"PG\":\"F1\",\"RD\":[{\"TM\":\"1970-01-01T00:00:00,000+00:00 S\",\"RV\":0,\"RI\":\"1-0:1.8.0\",\"RU\":\"kWh\",\"RT\":\"AC\",\"ST\":\"G\"}]}|{\"SA\":\"ECDSA-secp384r1-SHA256\",\"SE\":\"base64\",\"SD\":\"MGUCMQDBfiYEJP6l6Y4jE4F12Q0D6S4C9u5KNWkE7K3Xpo0VtEthCaoLtAD7FSHhX6bQCw4CMHVvmyOEj41sGc//JgEy3W8OTJXgc5LPMvYovG2BUrgU/71z1w9Bf2cVhukQ6wH7sg==\"}", buf);

	free((char *)buf);
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

	mid_sign_ctx_t *ctx = mid_sign_ctx_get_global();
	const char * buf = midocmf_signed_fiscal_from_meter_value(ctx, "ZAP000001", &meter_value, NULL);
	ESP_LOGI(TAG, "%s", buf);

	TEST_ASSERT_EQUAL_STRING("OCMF|{\"FV\":\"1.0\",\"GI\":\"Zaptec Go+\",\"GS\":\"ZAP000001\",\"GV\":\"2.0.4.201\",\"MF\":\"v1.2.3\",\"PG\":\"F1\",\"RD\":[{\"TM\":\"2024-01-30T07:46:02,999+00:00 S\",\"RV\":0,\"RI\":\"1-0:1.8.0\",\"RU\":\"kWh\",\"RT\":\"AC\",\"ST\":\"G\"}]}|{\"SA\":\"ECDSA-secp384r1-SHA256\",\"SE\":\"base64\",\"SD\":\"MGUCMQDFpIau4+6ECCvQJbCKWRg6nu4O5ie39RL5JIQFFmEgMfLu0BPgBEGc0/zYuLqAMD4CMGMSlDLWZjDDhpPTJyHcDq1FkKK3ZDn50U5wLxFfKNXdqrlH5fnsVgimokVEi9GH7Q==\"}", buf);
	free((char *)buf);
}

TEST_CASE("Test OCMF transaction - simple", "[ocmf]") {
	midlts_active_t active = {};
	midlts_active_session_alloc(&active);

	uint64_t time = 1706600762ULL * 1000 + 999;
	mid_session_meter_value_t meter_value = { .fw = fw, .lr = lr, .time = time, .meter = 0, .flag = MID_SESSION_METER_VALUE_READING_FLAG_TARIFF | MID_SESSION_METER_VALUE_FLAG_TIME_SYNCHRONIZED };

	midlts_active_session_append(&active, &meter_value);

	const char * buf = midocmf_signed_transaction_from_active_session(NULL, "ZAP000001", &active);

	TEST_ASSERT_EQUAL_STRING("OCMF|{\"FV\":\"1.0\",\"GI\":\"Zaptec Go+\",\"GS\":\"ZAP000001\",\"GV\":\"2.0.4.201\",\"MF\":\"v1.2.3\",\"PG\":\"T1\",\"IS\":false,\"RD\":[{\"TM\":\"2024-01-30T07:46:02,999+00:00 S\",\"TX\":\"T\",\"RU\":\"kWh\",\"RI\":\"1-0:1.8.0\",\"RV\":0,\"ST\":\"G\"}]}", buf);
	free((char *)buf);

	midlts_active_session_free(&active);
}

TEST_CASE("Test OCMF transaction - Session ID", "[ocmf]") {
	midlts_active_t active = {};
	midlts_active_session_alloc(&active);

	uint64_t time = 1706600762ULL * 1000 + 999;
	mid_session_meter_value_t meter_value = { .fw = fw, .lr = lr, .time = time, .meter = 0, .flag = MID_SESSION_METER_VALUE_READING_FLAG_TARIFF | MID_SESSION_METER_VALUE_FLAG_TIME_SYNCHRONIZED };

	mid_session_id_t id = { .uuid = { 0xca, 0xfe, 0xba, 0xbe } };

	midlts_active_session_set_id(&active, &id);
	midlts_active_session_append(&active, &meter_value);

	const char * buf = midocmf_signed_transaction_from_active_session(NULL, "ZAP000001", &active);
	TEST_ASSERT_EQUAL_STRING("OCMF|{\"FV\":\"1.0\",\"GI\":\"Zaptec Go+\",\"GS\":\"ZAP000001\",\"GV\":\"2.0.4.201\",\"MF\":\"v1.2.3\",\"PG\":\"T1\",\"IS\":false,\"RD\":[{\"TM\":\"2024-01-30T07:46:02,999+00:00 S\",\"TX\":\"T\",\"RU\":\"kWh\",\"RI\":\"1-0:1.8.0\",\"RV\":0,\"ST\":\"G\"}],\"ZS\":\"cafebabe-0000-0000-0000-000000000000\"}", buf);
	free((char *)buf);

	midlts_active_session_free(&active);
}


TEST_CASE("Test OCMF transaction - Auth RFID ISO14443", "[ocmf]") {
	midlts_active_t active = {};
	midlts_active_session_alloc(&active);

	uint64_t time = 1706600762ULL * 1000 + 999;
	mid_session_meter_value_t meter_value = { .fw = fw, .lr = lr, .time = time, .meter = 0, .flag = MID_SESSION_METER_VALUE_READING_FLAG_TARIFF | MID_SESSION_METER_VALUE_FLAG_TIME_SYNCHRONIZED };

	mid_session_auth_t auth = { .source = MID_SESSION_AUTH_SOURCE_RFID, .type = MID_SESSION_AUTH_TYPE_RFID, .length = 4, .tag = { 0xab, 0xcd, 0xef, 0x12 } };
	midlts_active_session_set_auth(&active, &auth);

	midlts_active_session_append(&active, &meter_value);

	const char * buf = midocmf_signed_transaction_from_active_session(NULL, "ZAP000001", &active);

	TEST_ASSERT_EQUAL_STRING("OCMF|{\"FV\":\"1.0\",\"GI\":\"Zaptec Go+\",\"GS\":\"ZAP000001\",\"GV\":\"2.0.4.201\",\"MF\":\"v1.2.3\",\"PG\":\"T1\",\"IS\":true,\"IL\":\"HEARSAY\",\"IF\":[\"RFID_RELATED\"],\"IT\":\"ISO14443\",\"ID\":\"ABCDEF12\",\"RD\":[{\"TM\":\"2024-01-30T07:46:02,999+00:00 S\",\"TX\":\"T\",\"RU\":\"kWh\",\"RI\":\"1-0:1.8.0\",\"RV\":0,\"ST\":\"G\"}]}", buf);

	free((char *)buf);
	midlts_active_session_free(&active);
}

TEST_CASE("Test OCMF transaction - Auth RFID ISO15693", "[ocmf]") {
	midlts_active_t active = {};
	midlts_active_session_alloc(&active);

	uint64_t time = 1706600762ULL * 1000 + 999;
	mid_session_meter_value_t meter_value = { .fw = fw, .lr = lr, .time = time, .meter = 0, .flag = MID_SESSION_METER_VALUE_READING_FLAG_TARIFF | MID_SESSION_METER_VALUE_FLAG_TIME_SYNCHRONIZED };

	mid_session_auth_t auth = { .source = MID_SESSION_AUTH_SOURCE_RFID, .type = MID_SESSION_AUTH_TYPE_RFID, .length = 8, .tag = { 0xab, 0xcd, 0xef, 0x12, 0x34, 0x45, 0x56, 0x67 } };
	midlts_active_session_set_auth(&active, &auth);

	midlts_active_session_append(&active, &meter_value);

	const char * buf = midocmf_signed_transaction_from_active_session(NULL, "ZAP000001", &active);

	TEST_ASSERT_EQUAL_STRING("OCMF|{\"FV\":\"1.0\",\"GI\":\"Zaptec Go+\",\"GS\":\"ZAP000001\",\"GV\":\"2.0.4.201\",\"MF\":\"v1.2.3\",\"PG\":\"T1\",\"IS\":true,\"IL\":\"HEARSAY\",\"IF\":[\"RFID_RELATED\"],\"IT\":\"ISO15693\",\"ID\":\"ABCDEF1234455667\",\"RD\":[{\"TM\":\"2024-01-30T07:46:02,999+00:00 S\",\"TX\":\"T\",\"RU\":\"kWh\",\"RI\":\"1-0:1.8.0\",\"RV\":0,\"ST\":\"G\"}]}", buf);
	free((char *)buf);

	midlts_active_session_free(&active);
}

TEST_CASE("Test OCMF transaction - Auth Cloud RFID", "[ocmf]") {
	midlts_active_t active = {};
	midlts_active_session_alloc(&active);

	uint64_t time = 1706600762ULL * 1000 + 999;
	mid_session_meter_value_t meter_value = { .fw = fw, .lr = lr, .time = time, .meter = 0, .flag = MID_SESSION_METER_VALUE_READING_FLAG_TARIFF | MID_SESSION_METER_VALUE_FLAG_TIME_SYNCHRONIZED };

	mid_session_auth_t auth = { .source = MID_SESSION_AUTH_SOURCE_CLOUD, .type = MID_SESSION_AUTH_TYPE_RFID, .length = 8, .tag = { 0xab, 0xcd, 0xef, 0x12, 0x34, 0x45, 0x56, 0x67 } };
	midlts_active_session_set_auth(&active, &auth);

	midlts_active_session_append(&active, &meter_value);

	const char * buf = midocmf_signed_transaction_from_active_session(NULL, "ZAP000001", &active);

	TEST_ASSERT_EQUAL_STRING("OCMF|{\"FV\":\"1.0\",\"GI\":\"Zaptec Go+\",\"GS\":\"ZAP000001\",\"GV\":\"2.0.4.201\",\"MF\":\"v1.2.3\",\"PG\":\"T1\",\"IS\":true,\"IL\":\"TRUSTED\",\"IT\":\"ISO15693\",\"ID\":\"ABCDEF1234455667\",\"RD\":[{\"TM\":\"2024-01-30T07:46:02,999+00:00 S\",\"TX\":\"T\",\"RU\":\"kWh\",\"RI\":\"1-0:1.8.0\",\"RV\":0,\"ST\":\"G\"}]}", buf);

	free((char *)buf);
	midlts_active_session_free(&active);
}

TEST_CASE("Test OCMF transaction - Auth Cloud UUID", "[ocmf]") {
	midlts_active_t active = {};
	midlts_active_session_alloc(&active);

	uint64_t time = 1706600762ULL * 1000 + 999;
	mid_session_meter_value_t meter_value = { .fw = fw, .lr = lr, .time = time, .meter = 0, .flag = MID_SESSION_METER_VALUE_READING_FLAG_TARIFF | MID_SESSION_METER_VALUE_FLAG_TIME_SYNCHRONIZED };

	mid_session_auth_t auth = { .source = MID_SESSION_AUTH_SOURCE_CLOUD, .type = MID_SESSION_AUTH_TYPE_UUID, .length = 16, .tag = { 0xab, 0xcd, 0xef, 0x12, 0x34, 0x45, 0x56, 0x67, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x11, 0x22 } };
	midlts_active_session_set_auth(&active, &auth);

	midlts_active_session_append(&active, &meter_value);

	const char * buf = midocmf_signed_transaction_from_active_session(NULL, "ZAP000001", &active);

	TEST_ASSERT_EQUAL_STRING("OCMF|{\"FV\":\"1.0\",\"GI\":\"Zaptec Go+\",\"GS\":\"ZAP000001\",\"GV\":\"2.0.4.201\",\"MF\":\"v1.2.3\",\"PG\":\"T1\",\"IS\":true,\"IL\":\"TRUSTED\",\"IT\":\"CENTRAL\",\"ID\":\"abcdef12-3445-5667-aabb-ccddeeff1122\",\"RD\":[{\"TM\":\"2024-01-30T07:46:02,999+00:00 S\",\"TX\":\"T\",\"RU\":\"kWh\",\"RI\":\"1-0:1.8.0\",\"RV\":0,\"ST\":\"G\"}]}", buf);
	free((char *)buf);

	midlts_active_session_free(&active);
}

TEST_CASE("Test OCMF transaction - Different flags", "[ocmf]") {
	midlts_active_t active = {};
	midlts_active_session_alloc(&active);

	uint64_t time = 1706600762ULL * 1000 + 999;
	mid_session_meter_value_t meter_value[] = {
		{ .fw = fw, .lr = lr, .time = time, .meter = 0, .flag = MID_SESSION_METER_VALUE_READING_FLAG_START | MID_SESSION_METER_VALUE_FLAG_TIME_SYNCHRONIZED },
		{ .fw = fw, .lr = lr, .time = time + 1 * 60 * 60 * 1000, .meter = 10, .flag = MID_SESSION_METER_VALUE_READING_FLAG_TARIFF | MID_SESSION_METER_VALUE_FLAG_TIME_INFORMATIVE },
		{ .fw = fw, .lr = lr, .time = time + 2 * 60 * 60 * 1000, .meter = 20, .flag = MID_SESSION_METER_VALUE_READING_FLAG_TARIFF | MID_SESSION_METER_VALUE_FLAG_TIME_RELATIVE },
		{ .fw = fw, .lr = lr, .time = time + 3 * 60 * 60 * 1000, .meter = 30, .flag = MID_SESSION_METER_VALUE_READING_FLAG_TARIFF | MID_SESSION_METER_VALUE_FLAG_TIME_UNKNOWN },
		{ .fw = fw, .lr = lr, .time = time + 4 * 60 * 60 * 1000, .meter = 40, .flag = MID_SESSION_METER_VALUE_READING_FLAG_TARIFF | MID_SESSION_METER_VALUE_FLAG_TIME_INFORMATIVE | MID_SESSION_METER_VALUE_FLAG_METER_ERROR },
		{ .fw = fw, .lr = lr, .time = time + 5 * 60 * 60 * 1000, .meter = 50, .flag = MID_SESSION_METER_VALUE_READING_FLAG_END | MID_SESSION_METER_VALUE_FLAG_TIME_SYNCHRONIZED },
	};

	for (size_t i = 0; i < sizeof (meter_value) / sizeof (meter_value[0]); i++) {
		midlts_active_session_append(&active, &meter_value[i]);
	}

	const char * buf = midocmf_signed_transaction_from_active_session(NULL, "ZAP000001", &active);

	TEST_ASSERT_EQUAL_STRING("OCMF|{\"FV\":\"1.0\",\"GI\":\"Zaptec Go+\",\"GS\":\"ZAP000001\",\"GV\":\"2.0.4.201\",\"MF\":\"v1.2.3\",\"PG\":\"T1\",\"IS\":false,\"RD\":[{\"TM\":\"2024-01-30T07:46:02,999+00:00 S\",\"TX\":\"B\",\"RU\":\"kWh\",\"RI\":\"1-0:1.8.0\",\"RV\":0,\"ST\":\"G\"},{\"TM\":\"2024-01-30T08:46:02,999+00:00 I\",\"TX\":\"T\",\"RI\":\"1-0:1.8.0\",\"RV\":0.01,\"ST\":\"G\"},{\"TM\":\"2024-01-30T09:46:02,999+00:00 R\",\"TX\":\"T\",\"RI\":\"1-0:1.8.0\",\"RV\":0.02,\"ST\":\"G\"},{\"TM\":\"2024-01-30T10:46:02,999+00:00 U\",\"TX\":\"T\",\"RI\":\"1-0:1.8.0\",\"RV\":0.03,\"ST\":\"G\"},{\"TM\":\"2024-01-30T11:46:02,999+00:00 I\",\"TX\":\"T\",\"ST\":\"E\"},{\"TM\":\"2024-01-30T12:46:02,999+00:00 S\",\"TX\":\"E\",\"RI\":\"1-0:1.8.0\",\"RV\":0.05,\"ST\":\"G\"}]}", buf);
	free((char *)buf);

	midlts_active_session_free(&active);
}

TEST_CASE("Test OCMF transaction - Different flags, signed", "[ocmf]") {
	midlts_active_t active;
	midlts_active_session_alloc(&active);

	uint64_t time = 1706600762ULL * 1000 + 999;
	mid_session_meter_value_t meter_value[] = {
		{ .fw = fw, .lr = lr, .time = time, .meter = 0, .flag = MID_SESSION_METER_VALUE_READING_FLAG_START | MID_SESSION_METER_VALUE_FLAG_TIME_SYNCHRONIZED },
		{ .fw = fw, .lr = lr, .time = time + 1 * 60 * 60 * 1000, .meter = 10, .flag = MID_SESSION_METER_VALUE_READING_FLAG_TARIFF | MID_SESSION_METER_VALUE_FLAG_TIME_INFORMATIVE },
		{ .fw = fw, .lr = lr, .time = time + 2 * 60 * 60 * 1000, .meter = 20, .flag = MID_SESSION_METER_VALUE_READING_FLAG_TARIFF | MID_SESSION_METER_VALUE_FLAG_TIME_RELATIVE },
		{ .fw = fw, .lr = lr, .time = time + 3 * 60 * 60 * 1000, .meter = 30, .flag = MID_SESSION_METER_VALUE_READING_FLAG_TARIFF | MID_SESSION_METER_VALUE_FLAG_TIME_UNKNOWN },
		{ .fw = fw, .lr = lr, .time = time + 4 * 60 * 60 * 1000, .meter = 40, .flag = MID_SESSION_METER_VALUE_READING_FLAG_TARIFF | MID_SESSION_METER_VALUE_FLAG_TIME_INFORMATIVE | MID_SESSION_METER_VALUE_FLAG_METER_ERROR },
		{ .fw = fw, .lr = lr, .time = time + 5 * 60 * 60 * 1000, .meter = 50, .flag = MID_SESSION_METER_VALUE_READING_FLAG_END | MID_SESSION_METER_VALUE_FLAG_TIME_SYNCHRONIZED },
	};

	for (size_t i = 0; i < sizeof (meter_value) / sizeof (meter_value[0]); i++) {
		midlts_active_session_append(&active, &meter_value[i]);
	}

	mid_sign_ctx_t *ctx = mid_sign_ctx_get_global();
	const char * buf = midocmf_signed_transaction_from_active_session(ctx, "ZAP000001", &active);

	TEST_ASSERT_EQUAL_STRING("OCMF|{\"FV\":\"1.0\",\"GI\":\"Zaptec Go+\",\"GS\":\"ZAP000001\",\"GV\":\"2.0.4.201\",\"MF\":\"v1.2.3\",\"PG\":\"T1\",\"IS\":false,\"RD\":[{\"TM\":\"2024-01-30T07:46:02,999+00:00 S\",\"TX\":\"B\",\"RU\":\"kWh\",\"RI\":\"1-0:1.8.0\",\"RV\":0,\"ST\":\"G\"},{\"TM\":\"2024-01-30T08:46:02,999+00:00 I\",\"TX\":\"T\",\"RI\":\"1-0:1.8.0\",\"RV\":0.01,\"ST\":\"G\"},{\"TM\":\"2024-01-30T09:46:02,999+00:00 R\",\"TX\":\"T\",\"RI\":\"1-0:1.8.0\",\"RV\":0.02,\"ST\":\"G\"},{\"TM\":\"2024-01-30T10:46:02,999+00:00 U\",\"TX\":\"T\",\"RI\":\"1-0:1.8.0\",\"RV\":0.03,\"ST\":\"G\"},{\"TM\":\"2024-01-30T11:46:02,999+00:00 I\",\"TX\":\"T\",\"ST\":\"E\"},{\"TM\":\"2024-01-30T12:46:02,999+00:00 S\",\"TX\":\"E\",\"RI\":\"1-0:1.8.0\",\"RV\":0.05,\"ST\":\"G\"}]}|{\"SA\":\"ECDSA-secp384r1-SHA256\",\"SE\":\"base64\",\"SD\":\"MGUCMFmT53qOe0h/vVgIBUc5Hzk2iGYsw1c23ECORvWXd1Xfn3jAz4tAfzWjxHJsMM7ZQgIxAOg1SejcEGNoqxjvafFzTdJEUfmAZiuVhgaxInO5vOYus1T3kZ1vBROgwy2rcU2Jxw==\"}", buf);

	ESP_LOGI(TAG, "%s", buf);
	free((char *)buf);

	midlts_active_session_free(&active);
}

TEST_CASE("Test OCMF transaction - Different flags, ID & auth signed", "[ocmf]") {
	midlts_active_t active;
	midlts_active_session_alloc(&active);

	uint64_t time = 1706600762ULL * 1000 + 999;
	mid_session_meter_value_t meter_value[] = {
		{ .fw = fw, .lr = lr, .time = time, .meter = 0, .flag = MID_SESSION_METER_VALUE_READING_FLAG_START | MID_SESSION_METER_VALUE_FLAG_TIME_SYNCHRONIZED },
		{ .fw = fw, .lr = lr, .time = time + 1 * 60 * 60 * 1000, .meter = 10, .flag = MID_SESSION_METER_VALUE_READING_FLAG_TARIFF | MID_SESSION_METER_VALUE_FLAG_TIME_INFORMATIVE },
		{ .fw = fw, .lr = lr, .time = time + 2 * 60 * 60 * 1000, .meter = 20, .flag = MID_SESSION_METER_VALUE_READING_FLAG_TARIFF | MID_SESSION_METER_VALUE_FLAG_TIME_RELATIVE },
		{ .fw = fw, .lr = lr, .time = time + 3 * 60 * 60 * 1000, .meter = 30, .flag = MID_SESSION_METER_VALUE_READING_FLAG_TARIFF | MID_SESSION_METER_VALUE_FLAG_TIME_UNKNOWN },
		{ .fw = fw, .lr = lr, .time = time + 4 * 60 * 60 * 1000, .meter = 40, .flag = MID_SESSION_METER_VALUE_READING_FLAG_TARIFF | MID_SESSION_METER_VALUE_FLAG_TIME_INFORMATIVE | MID_SESSION_METER_VALUE_FLAG_METER_ERROR },
		{ .fw = fw, .lr = lr, .time = time + 5 * 60 * 60 * 1000, .meter = 50, .flag = MID_SESSION_METER_VALUE_READING_FLAG_END | MID_SESSION_METER_VALUE_FLAG_TIME_SYNCHRONIZED },
	};

	for (size_t i = 0; i < sizeof (meter_value) / sizeof (meter_value[0]); i++) {
		midlts_active_session_append(&active, &meter_value[i]);
	}

	mid_session_id_t id = { .uuid = { 0xca, 0xfe, 0xba, 0xbe } };
	midlts_active_session_set_id(&active, &id);

	mid_session_auth_t auth = { .source = MID_SESSION_AUTH_SOURCE_RFID, .type = MID_SESSION_AUTH_TYPE_RFID, .length = 4, .tag = { 0xab, 0xcd, 0xef, 0x12 } };
	midlts_active_session_set_auth(&active, &auth);

	mid_sign_ctx_t *ctx = mid_sign_ctx_get_global();
	const char * buf = midocmf_signed_transaction_from_active_session(ctx, "ZAP000001", &active);

	TEST_ASSERT_EQUAL_STRING("OCMF|{\"FV\":\"1.0\",\"GI\":\"Zaptec Go+\",\"GS\":\"ZAP000001\",\"GV\":\"2.0.4.201\",\"MF\":\"v1.2.3\",\"PG\":\"T1\",\"IS\":true,\"IL\":\"HEARSAY\",\"IF\":[\"RFID_RELATED\"],\"IT\":\"ISO14443\",\"ID\":\"ABCDEF12\",\"RD\":[{\"TM\":\"2024-01-30T07:46:02,999+00:00 S\",\"TX\":\"B\",\"RU\":\"kWh\",\"RI\":\"1-0:1.8.0\",\"RV\":0,\"ST\":\"G\"},{\"TM\":\"2024-01-30T08:46:02,999+00:00 I\",\"TX\":\"T\",\"RI\":\"1-0:1.8.0\",\"RV\":0.01,\"ST\":\"G\"},{\"TM\":\"2024-01-30T09:46:02,999+00:00 R\",\"TX\":\"T\",\"RI\":\"1-0:1.8.0\",\"RV\":0.02,\"ST\":\"G\"},{\"TM\":\"2024-01-30T10:46:02,999+00:00 U\",\"TX\":\"T\",\"RI\":\"1-0:1.8.0\",\"RV\":0.03,\"ST\":\"G\"},{\"TM\":\"2024-01-30T11:46:02,999+00:00 I\",\"TX\":\"T\",\"ST\":\"E\"},{\"TM\":\"2024-01-30T12:46:02,999+00:00 S\",\"TX\":\"E\",\"RI\":\"1-0:1.8.0\",\"RV\":0.05,\"ST\":\"G\"}],\"ZS\":\"cafebabe-0000-0000-0000-000000000000\"}|{\"SA\":\"ECDSA-secp384r1-SHA256\",\"SE\":\"base64\",\"SD\":\"MGUCMFYt+uOW4fNGy4E+FOH8oY/DA+srKK7l3L6eZ1GoJHStu7N8rV/izaG/89R6tYQ6BgIxAPhhrlMiPdjymesfH1aEOq1nFq4gvLBSgxiyhHiwneBvF+l7PkQiUyAoSvvzft/z3w==\"}", buf);

	ESP_LOGI(TAG, "%s", buf);
	free((char *)buf);

	midlts_active_session_free(&active);
}
