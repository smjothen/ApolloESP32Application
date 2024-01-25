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

TEST_CASE("Test OCMF signed energy - No flag", "[ocmf][allowleak]") {
	mid_session_meter_value_t meter_value = { .fw = fw, .lr = lr, .time = MID_TIME_PACK(MID_EPOCH), .meter = 0, .flag = MID_SESSION_METER_VALUE_READING_FLAG_TARIFF };

	char buf[512];
	TEST_ASSERT_EQUAL_INT(0, midocmf_fiscal_from_meter_value(buf, sizeof (buf), "ZAP000001", &meter_value, NULL));

	TEST_ASSERT_EQUAL_STRING("OCMF|{\"FV\":\"1.0\",\"GI\":\"Zaptec Go Plus\",\"GS\":\"ZAP000001\",\"GV\":\"2.0.4.201\",\"MF\":\"v1.2.3\",\"PG\":\"F1\",\"RD\":[{\"TM\":\"2020-01-01T00:00:00,000+00:00 U\",\"RV\":0,\"RI\":\"1-0:1.8.0\",\"RU\":\"kWh\",\"RT\":\"AC\",\"ST\":\"G\"}]}", buf);
}

TEST_CASE("Test OCMF signed energy only for tariff changes", "[ocmf]") {
	mid_session_meter_value_t meter_value = { .fw = fw, .lr = lr, .time = MID_TIME_PACK(MID_EPOCH), .meter = 0, .flag = 0 };

	char buf[512];

	// Not a tariff change (flag = 0)
	TEST_ASSERT_EQUAL_INT(-1, midocmf_fiscal_from_meter_value(buf, sizeof (buf), "ZAP000001", &meter_value, NULL));
}

TEST_CASE("Test OCMF signed energy - Unknown", "[ocmf]") {
	mid_session_meter_value_t meter_value = { .fw = fw, .lr = lr, .time = MID_TIME_PACK(MID_EPOCH), .meter = 0, .flag = MID_SESSION_METER_VALUE_READING_FLAG_TARIFF | MID_SESSION_METER_VALUE_FLAG_TIME_UNKNOWN };

	char buf[512];
	TEST_ASSERT_EQUAL_INT(0, midocmf_fiscal_from_meter_value(buf, sizeof (buf), "ZAP000001", &meter_value, NULL));


	TEST_ASSERT_EQUAL_STRING("OCMF|{\"FV\":\"1.0\",\"GI\":\"Zaptec Go Plus\",\"GS\":\"ZAP000001\",\"GV\":\"2.0.4.201\",\"MF\":\"v1.2.3\",\"PG\":\"F1\",\"RD\":[{\"TM\":\"2020-01-01T00:00:00,000+00:00 U\",\"RV\":0,\"RI\":\"1-0:1.8.0\",\"RU\":\"kWh\",\"RT\":\"AC\",\"ST\":\"G\"}]}", buf);
}

TEST_CASE("Test OCMF signed energy - Sync", "[ocmf]") {
	mid_session_meter_value_t meter_value = { .fw = fw, .lr = lr, .time = MID_TIME_PACK(MID_EPOCH), .meter = 0, .flag = MID_SESSION_METER_VALUE_READING_FLAG_TARIFF | MID_SESSION_METER_VALUE_FLAG_TIME_SYNCHRONIZED };
	char buf[512];
	TEST_ASSERT_EQUAL_INT(0, midocmf_fiscal_from_meter_value(buf, sizeof (buf), "ZAP000001", &meter_value, NULL));
	TEST_ASSERT_EQUAL_STRING("OCMF|{\"FV\":\"1.0\",\"GI\":\"Zaptec Go Plus\",\"GS\":\"ZAP000001\",\"GV\":\"2.0.4.201\",\"MF\":\"v1.2.3\",\"PG\":\"F1\",\"RD\":[{\"TM\":\"2020-01-01T00:00:00,000+00:00 S\",\"RV\":0,\"RI\":\"1-0:1.8.0\",\"RU\":\"kWh\",\"RT\":\"AC\",\"ST\":\"G\"}]}", buf);
}

TEST_CASE("Test OCMF signed energy - Relative", "[ocmf]") {
	mid_session_meter_value_t meter_value = { .fw = fw, .lr = lr, .time = MID_TIME_PACK(MID_EPOCH), .meter = 0, .flag = MID_SESSION_METER_VALUE_READING_FLAG_TARIFF | MID_SESSION_METER_VALUE_FLAG_TIME_RELATIVE };
	char buf[512];
	TEST_ASSERT_EQUAL_INT(0, midocmf_fiscal_from_meter_value(buf, sizeof (buf), "ZAP000001", &meter_value, NULL));
	TEST_ASSERT_EQUAL_STRING("OCMF|{\"FV\":\"1.0\",\"GI\":\"Zaptec Go Plus\",\"GS\":\"ZAP000001\",\"GV\":\"2.0.4.201\",\"MF\":\"v1.2.3\",\"PG\":\"F1\",\"RD\":[{\"TM\":\"2020-01-01T00:00:00,000+00:00 R\",\"RV\":0,\"RI\":\"1-0:1.8.0\",\"RU\":\"kWh\",\"RT\":\"AC\",\"ST\":\"G\"}]}", buf);
}

TEST_CASE("Test OCMF signed energy - Informative", "[ocmf]") {
	mid_session_meter_value_t meter_value = { .fw = fw, .lr = lr, .time = MID_TIME_PACK(MID_EPOCH), .meter = 0, .flag = MID_SESSION_METER_VALUE_READING_FLAG_TARIFF | MID_SESSION_METER_VALUE_FLAG_TIME_INFORMATIVE };
	char buf[512];
	TEST_ASSERT_EQUAL_INT(0, midocmf_fiscal_from_meter_value(buf, sizeof (buf), "ZAP000001", &meter_value, NULL));
	TEST_ASSERT_EQUAL_STRING("OCMF|{\"FV\":\"1.0\",\"GI\":\"Zaptec Go Plus\",\"GS\":\"ZAP000001\",\"GV\":\"2.0.4.201\",\"MF\":\"v1.2.3\",\"PG\":\"F1\",\"RD\":[{\"TM\":\"2020-01-01T00:00:00,000+00:00 I\",\"RV\":0,\"RI\":\"1-0:1.8.0\",\"RU\":\"kWh\",\"RT\":\"AC\",\"ST\":\"G\"}]}", buf);
}
