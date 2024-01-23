#include <time.h>
#include <limits.h>
#include "unity.h"
#include "esp_log.h"
#include "mid_lts.h"
#include "mid_ocmf.h"
#include "mid_lts_test.h"

static const char *TAG = "MIDTEST";

#define RESET TEST_ASSERT(mid_session_reset() == LTS_OK)

static const mid_session_version_fw_t default_fw = { 2, 0, 4, 201 };
static const mid_session_version_lr_t default_lr = { 1, 2, 3 };

TEST_CASE("Test no leakages", "[mid]") {
	RESET;

	midlts_ctx_t ctx;
	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_init(&ctx, 0, default_fw, default_lr));
}

TEST_CASE("Test session closed when not open", "[mid]") {
	RESET;

	midlts_ctx_t ctx;
	midlts_pos_t pos;

	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_init(&ctx, 0, default_fw, default_lr));
	TEST_ASSERT_EQUAL_INT(LTS_SESSION_NOT_OPEN, mid_session_add_close(&ctx, &pos, NULL, 0, 0, 0));
	TEST_ASSERT(MID_SESSION_IS_CLOSED(&ctx));
}

// Allow leak (probably it is caching the file descriptor or buffers)?
TEST_CASE("Test session open close", "[mid][allowleak]") {
	RESET;

	midlts_ctx_t ctx;
	midlts_pos_t pos;

	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_init(&ctx, 0, default_fw, default_lr));
	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_add_open(&ctx, &pos, NULL, 0, 0, 0));
	TEST_ASSERT(MID_SESSION_IS_OPEN(&ctx));
	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_add_close(&ctx, &pos, NULL, 0, 0, 0));
	TEST_ASSERT(MID_SESSION_IS_CLOSED(&ctx));

	// TODO: Readback with pos and verify
}

TEST_CASE("Test OCMF serialization of fiscal message", "[mid]") {
	RESET;

	midlts_ctx_t ctx;
	midlts_pos_t pos;
	mid_session_record_t rec[2];

	time_t now = MID_EPOCH;

	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_init(&ctx, 0, default_fw, default_lr));
	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_add_open(&ctx, &pos, &rec[0], now, 0, 0));
	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_add_tariff(&ctx, &pos, &rec[1], now, 0, 0));

	char buf[512];

	// Not a meter value
	TEST_ASSERT_EQUAL_INT(-1, midocmf_create_fiscal_message(buf, sizeof (buf), "ZAP000001", &rec[0]));
	TEST_ASSERT_EQUAL_INT(0, midocmf_create_fiscal_message(buf, sizeof (buf), "ZAP000001", &rec[1]));
	ESP_LOGI(TAG, "%s", buf);

	const char *expected = "OCMF|{\"FV\":\"1.0\",\"GI\":\"Zaptec Go Plus\",\"GS\":\"ZAP000001\",\"GV\":\"2.0.4.201\",\"MF\":\"v1.2.3\",\"PG\":\"F1\",\"RD\":[{\"TM\":\"2020-01-01T00:00:00,000+00:00 U\",\"RV\":0,\"RI\":\"1-0:1.8.0\",\"RU\":\"kWh\",\"RT\":\"AC\",\"ST\":\"G\"}]}";
	TEST_ASSERT_EQUAL_STRING(expected, buf);
}

TEST_CASE("Test session flag persistence", "[mid]") {
	RESET;

	midlts_ctx_t ctx;
	midlts_pos_t pos;

	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_init(&ctx, 0, default_fw, default_lr));
	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_add_open(&ctx, &pos, NULL, 0, 0, 0));

	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_init(&ctx, 0, default_fw, default_lr));
	TEST_ASSERT(MID_SESSION_IS_OPEN(&ctx));

	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_add_close(&ctx, &pos, NULL, 0, 0, 0));
	TEST_ASSERT(MID_SESSION_IS_CLOSED(&ctx));

	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_init(&ctx, 0, default_fw, default_lr));
	TEST_ASSERT(MID_SESSION_IS_CLOSED(&ctx));
}

TEST_CASE("Test session open twice", "[mid]") {
	RESET;

	midlts_ctx_t ctx;
	midlts_pos_t pos;

	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_init(&ctx, 0, default_fw, default_lr));
	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_add_open(&ctx, &pos, NULL, 0, 0, 0));
	TEST_ASSERT_EQUAL_INT(LTS_SESSION_ALREADY_OPEN, mid_session_add_open(&ctx, &pos, NULL, 0,  0, 0));
	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_add_close(&ctx, &pos, NULL, 0, 0, 0));
}

TEST_CASE("Test tariff change allowed out of session", "[mid]") {
	RESET;

	midlts_ctx_t ctx;
	midlts_pos_t pos;


	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_init(&ctx, 0, default_fw, default_lr));
	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_add_open(&ctx, &pos, NULL, 0, 0, 0));
	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_add_tariff(&ctx, &pos, NULL, 0, 0, 0));
	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_add_close(&ctx, &pos, NULL, 0, 0, 0));
	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_add_tariff(&ctx, &pos, NULL, 0, 0, 0));
}

TEST_CASE("Test persistence", "[mid]") {
	RESET;

	midlts_ctx_t ctx;
	midlts_pos_t pos;

	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_init(&ctx, 0, default_fw, default_lr));
	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_add_open(&ctx, &pos, NULL, 0, 0, 0));
	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_add_tariff(&ctx, &pos, NULL, 0, 0, 0));
	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_add_close(&ctx, &pos, NULL, 0, 0, 0));
	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_add_tariff(&ctx, &pos, NULL, 0, 0, 0));

	midlts_ctx_t ctx2;
	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_init(&ctx2, 0, default_fw, default_lr));
	TEST_ASSERT_EQUAL_INT(0, memcmp(&ctx, &ctx2, sizeof (ctx)));
}

TEST_CASE("Test persistence over multiple pages", "[mid]") {
	RESET;

	midlts_ctx_t ctx;
	midlts_pos_t pos;

	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_init(&ctx, 0, default_fw, default_lr));

	for (size_t i = 0; i < 128; i++) {
		TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_add_tariff(&ctx, &pos, NULL, 0, 0, 0));
	}

	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_init(&ctx, 0, default_fw, default_lr));
	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_add_tariff(&ctx, &pos, NULL, 0, 0, 0));

	TEST_ASSERT_EQUAL_INT(0, remove("/mid/0.ms"));

	midlts_ctx_t ctx1;
	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_init(&ctx1, 0, default_fw, default_lr));
	TEST_ASSERT_EQUAL_INT(0, memcmp(&ctx, &ctx1, sizeof (ctx)));
}

TEST_CASE("Test wraparound", "[mid]") {
	RESET;

	midlts_ctx_t ctx;
	midlts_pos_t pos;

	// Max 4 pages
	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_init_internal(&ctx, 4, 0, default_fw, default_lr));

	uint32_t time = MID_EPOCH;
	uint32_t flag = MID_SESSION_METER_VALUE_READING_FLAG_TARIFF;
	uint32_t meter = 0;
	uint32_t count = 0;

	for (int i = 0; i < 128 * 4 + 1; i++) {
		TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_add_tariff(&ctx, &pos, NULL, time++, flag, meter));
		meter += 100;
		count++;
	}


	midlts_ctx_t ctx0;
	TEST_ASSERT_EQUAL_INT(mid_session_init_internal(&ctx0, 4, 0, default_fw, default_lr), LTS_OK);
	TEST_ASSERT_EQUAL_INT(0, memcmp(&ctx0, &ctx, sizeof (ctx)));

	// Should be around a bit over a wraparound for 4 files so 0 should be enough
	TEST_ASSERT_EQUAL_INT(0, remove("/mid/1.ms"));
	TEST_ASSERT_EQUAL_INT(0, remove("/mid/2.ms"));
	TEST_ASSERT_EQUAL_INT(0, remove("/mid/3.ms"));

	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_init_internal(&ctx0, 4, 0, default_fw, default_lr));
	TEST_ASSERT_EQUAL_INT(0, memcmp(&ctx0, &ctx, sizeof (ctx)));
}

TEST_CASE("Test reading records", "[mid]") {
	RESET;

	midlts_ctx_t ctx;
	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_init(&ctx, 0, default_fw, default_lr));

	uint32_t time = MID_EPOCH;
	uint32_t flag = MID_SESSION_METER_VALUE_READING_FLAG_TARIFF;
	uint32_t meter = 0;
	uint32_t count = 0;

	midlts_pos_t pos[8];

	for (int i = 0; i < 8; i++) {
		TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_add_tariff(&ctx, &pos[i], NULL, time++, flag, meter));
		meter += 100;
		count++;
	}

	mid_session_record_t rec;

	// Test bad reads
	midlts_pos_t badpos = pos[0];
	badpos.id++;
	TEST_ASSERT_NOT_EQUAL(LTS_OK, mid_session_read_record(&ctx, &badpos, &rec));
	badpos = pos[0];
	badpos.offset = 8 * 32;
	TEST_ASSERT_NOT_EQUAL(LTS_OK, mid_session_read_record(&ctx, &badpos, &rec));
	badpos = pos[0];
	badpos.crc = 0xffffffff;
	TEST_ASSERT_NOT_EQUAL(LTS_OK, mid_session_read_record(&ctx, &badpos, &rec));

	for (int i = 0; i < 8; i++) {
		// Test good read
		midlts_err_t e = mid_session_read_record(&ctx, &pos[i], &rec);
		TEST_ASSERT_EQUAL_INT(LTS_OK, e);
		TEST_ASSERT_EQUAL_INT(MID_SESSION_RECORD_TYPE_METER_VALUE, rec.rec_type);
	}
}

TEST_CASE("Test bad CRC returns an error", "[mid]") {
	RESET;

	midlts_ctx_t ctx;
	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_init(&ctx, 0, default_fw, default_lr));

	uint32_t time = 0;
	uint32_t flag = MID_SESSION_METER_VALUE_READING_FLAG_TARIFF;
	uint32_t meter = 0;
	uint32_t count = 0;

	midlts_pos_t pos[8];

	for (int i = 0; i < 8; i++) {
		TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_add_tariff(&ctx, &pos[i], NULL, time++, flag, meter));
		meter += 100;
		count++;
	}

	// Corrupt first record
	FILE *fp = fopen("/mid/0.ms", "r+");
	TEST_ASSERT_NOT_EQUAL(NULL, fp);
	TEST_ASSERT(fputc('z', fp) == 'z');
	TEST_ASSERT(!fclose(fp));

	TEST_ASSERT_EQUAL_INT(LTS_BAD_CRC, mid_session_init(&ctx, 0, default_fw, default_lr));
}

TEST_CASE("Test bad CRC returns an error last record", "[mid]") {
	RESET;

	midlts_ctx_t ctx;
	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_init(&ctx, 0, default_fw, default_lr));

	uint32_t time = 0;
	uint32_t flag = MID_SESSION_METER_VALUE_READING_FLAG_TARIFF;
	uint32_t meter = 0;
	uint32_t count = 0;

	midlts_pos_t pos[8];

	for (int i = 0; i < 8; i++) {
		TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_add_tariff(&ctx, &pos[i], NULL, time++, flag, meter));
		meter += 100;
		count++;
	}

	FILE *fp = fopen("/mid/0.ms", "r+");
	TEST_ASSERT_NOT_EQUAL(NULL, fp)
	TEST_ASSERT_EQUAL_INT(0, fseek(fp, 32 * 7 + 16, SEEK_SET));
	TEST_ASSERT(fputc('z', fp) == 'z');
	TEST_ASSERT(!fclose(fp));

	TEST_ASSERT_EQUAL_INT(LTS_BAD_CRC, mid_session_init(&ctx, 0, default_fw, default_lr));
}

TEST_CASE("Test position and reading", "[mid]") {
	RESET;

	midlts_ctx_t ctx;
	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_init(&ctx, 0, default_fw, default_lr));

	uint32_t time = MID_EPOCH;
	uint32_t flag = MID_SESSION_METER_VALUE_READING_FLAG_TARIFF;
	uint32_t meter = 0;
	uint32_t count = 0;

	midlts_pos_t pos[8];

	for (int i = 0; i < 8; i++) {
		TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_add_tariff(&ctx, &pos[i], NULL, time++, flag, meter));
		meter += 100;
		count++;
	}

	for (int i = 0; i < 8; i++) {
		mid_session_record_t rec;
		TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_read_record(&ctx, &pos[i], &rec));
	}

	for (int i = 0; i < 8; i++) {
		mid_session_record_t rec;
		pos[i].crc += 1;
		TEST_ASSERT_EQUAL_INT(LTS_BAD_CRC, mid_session_read_record(&ctx, &pos[i], &rec));
	}
}

TEST_CASE("Test purge fail", "[mid]") {
	RESET;

	midlts_ctx_t ctx;
	midlts_pos_t pos;
	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_init_internal(&ctx, 2, 0, default_fw, default_lr));

	uint32_t time = MID_EPOCH;
	uint32_t flag = MID_SESSION_METER_VALUE_READING_FLAG_TARIFF;
	uint32_t meter = 0;
	uint32_t count = 0;

	// Fill two full pages, next entry will try to delete the first page
	for (int i = 0; i < 128 * 2; i++) {
		TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_add_tariff(&ctx, &pos, NULL, time++, flag, meter));
		meter += 100;
		count++;
	}

	// Time will be epoch + 256, definitely not old enough to automatically purge
	TEST_ASSERT_NOT_EQUAL(LTS_OK, mid_session_add_tariff(&ctx, &pos, NULL, time++, flag, meter));
}

TEST_CASE("Test purge success", "[mid]") {
	RESET;

	midlts_ctx_t ctx;
	midlts_pos_t pos;
	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_init_internal(&ctx, 2, 0, default_fw, default_lr));

	uint32_t time = MID_EPOCH;
	uint32_t flag = MID_SESSION_METER_VALUE_READING_FLAG_TARIFF;
	uint32_t meter = 0;
	uint32_t count = 0;

	// Fill two full pages, next entry will try to delete the first page
	for (int i = 0; i < 128 * 2; i++) {
		TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_add_tariff(&ctx, &pos, NULL, time, flag, meter));
		meter += 100;
		// Ensures first page is old enough at time of deletion
		time += 20925;
		count++;
	}

	// Time will be epoch + 256, definitely not old enough to automatically purge
	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_add_tariff(&ctx, &pos, NULL, time, flag, meter));
}

