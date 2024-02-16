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
	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_init(&ctx, default_fw, default_lr));

	mid_session_free(&ctx);
}

TEST_CASE("Test active session", "[mid][allowleak]") {
	RESET;

	midlts_ctx_t ctx;

	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_init(&ctx, default_fw, default_lr));
	TEST_ASSERT_EQUAL_INT(LTS_SESSION_NOT_OPEN, mid_session_add_close(&ctx, NULL, NULL, MID_TIME_TO_TS(0), 0, 0));
	TEST_ASSERT_EQUAL_INT(0, ctx.active_session.count);

	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_add_open(&ctx, NULL, NULL, MID_TIME_TO_TS(0), 0, 0));
	TEST_ASSERT(MID_SESSION_IS_OPEN(&ctx));

	uint8_t empty[16] = {0};
	uint8_t uuid[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
	uint8_t uuid1[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 16};
	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_add_id(&ctx, NULL, NULL, MID_TIME_TO_TS(0), uuid));

	TEST_ASSERT_EQUAL_INT(1, ctx.active_session.count);
	TEST_ASSERT_EQUAL_MEMORY(uuid, ctx.active_session.id.uuid, sizeof (uuid));

	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_add_id(&ctx, NULL, NULL, MID_TIME_TO_TS(0), uuid1));
	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_add_close(&ctx, NULL, NULL,MID_TIME_TO_TS( 0), 0, 0));

	TEST_ASSERT(MID_SESSION_IS_CLOSED(&ctx));
	TEST_ASSERT_EQUAL_INT(2, ctx.active_session.count);

	TEST_ASSERT_EQUAL_MEMORY(uuid1, ctx.active_session.id.uuid, sizeof (uuid1));

	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_add_open(&ctx, NULL, NULL,MID_TIME_TO_TS( 0), 0, 0));

	TEST_ASSERT_EQUAL_MEMORY(empty, ctx.active_session.id.uuid, sizeof (uuid));

	mid_session_free(&ctx);
}

TEST_CASE("Test session closed when not open", "[mid]") {
	RESET;

	midlts_ctx_t ctx;
	midlts_pos_t pos;

	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_init(&ctx, default_fw, default_lr));
	TEST_ASSERT_EQUAL_INT(LTS_SESSION_NOT_OPEN, mid_session_add_close(&ctx, &pos, NULL, MID_TIME_TO_TS(0), 0, 0));
	TEST_ASSERT(MID_SESSION_IS_CLOSED(&ctx));

	mid_session_free(&ctx);
}

// Allow leak (probably it is caching the file descriptor or buffers)?
TEST_CASE("Test session open close", "[mid][allowleak]") {
	RESET;

	midlts_ctx_t ctx;
	midlts_pos_t pos;

	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_init(&ctx, default_fw, default_lr));
	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_add_open(&ctx, &pos, NULL, MID_TIME_TO_TS(0), 0, 0));
	TEST_ASSERT(MID_SESSION_IS_OPEN(&ctx));
	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_add_close(&ctx, &pos, NULL, MID_TIME_TO_TS(0), 0, 0));
	TEST_ASSERT(MID_SESSION_IS_CLOSED(&ctx));

	mid_session_free(&ctx);
}

TEST_CASE("Test OCMF serialization of fiscal message", "[mid]") {
	RESET;

	midlts_ctx_t ctx;
	midlts_pos_t pos;
	mid_session_record_t rec[2];

	const struct timespec ts = MID_TIME_TO_TS(0);

	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_init(&ctx, default_fw, default_lr));
	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_add_open(&ctx, &pos, &rec[0], ts, 0, 0));
	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_add_tariff(&ctx, &pos, &rec[1], ts, MID_SESSION_METER_VALUE_FLAG_TIME_UNKNOWN, 0));

	// Not a meter value
	TEST_ASSERT_EQUAL_INT(NULL, midocmf_signed_fiscal_from_record(NULL, "ZAP000001", &rec[0], NULL));
	const char *ocmf = midocmf_signed_fiscal_from_record(NULL, "ZAP000001", &rec[1], NULL);
	ESP_LOGI(TAG, "%s", ocmf);

	const char *expected = "OCMF|{\"FV\":\"1.0\",\"GI\":\"Zaptec Go+\",\"GS\":\"ZAP000001\",\"GV\":\"2.0.4.201\",\"MF\":\"v1.2.3\",\"PG\":\"F1\",\"RD\":[{\"TM\":\"1970-01-01T00:00:00,000+00:00 U\",\"RV\":0,\"RI\":\"1-0:1.8.0\",\"RU\":\"kWh\",\"RT\":\"AC\",\"ST\":\"G\"}]}";
	TEST_ASSERT_EQUAL_STRING(expected, ocmf);

	mid_session_free(&ctx);
}

static const struct timespec epoch = MID_TIME_TO_TS(0);

TEST_CASE("Test session flag persistence", "[mid]") {
	RESET;

	midlts_ctx_t ctx;
	midlts_pos_t pos;

	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_init(&ctx, default_fw, default_lr));
	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_add_open(&ctx, &pos, NULL, epoch, 0, 0));

	mid_session_free(&ctx);

	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_init(&ctx, default_fw, default_lr));
	TEST_ASSERT(MID_SESSION_IS_OPEN(&ctx));

	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_add_close(&ctx, &pos, NULL, epoch, 0, 0));
	TEST_ASSERT(MID_SESSION_IS_CLOSED(&ctx));

	mid_session_free(&ctx);

	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_init(&ctx, default_fw, default_lr));
	TEST_ASSERT(MID_SESSION_IS_CLOSED(&ctx));

	mid_session_free(&ctx);
}

TEST_CASE("Test session open twice", "[mid]") {
	RESET;

	midlts_ctx_t ctx;
	midlts_pos_t pos;

	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_init(&ctx, default_fw, default_lr));
	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_add_open(&ctx, &pos, NULL, epoch, 0, 0));
	TEST_ASSERT_EQUAL_INT(LTS_SESSION_ALREADY_OPEN, mid_session_add_open(&ctx, &pos, NULL, epoch,  0, 0));
	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_add_close(&ctx, &pos, NULL, epoch, 0, 0));

	mid_session_free(&ctx);
}

TEST_CASE("Test tariff change allowed out of session", "[mid]") {
	RESET;

	midlts_ctx_t ctx;
	midlts_pos_t pos;

	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_init(&ctx, default_fw, default_lr));
	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_add_open(&ctx, &pos, NULL, epoch, 0, 0));
	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_add_tariff(&ctx, &pos, NULL, epoch, 0, 0));
	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_add_close(&ctx, &pos, NULL, epoch, 0, 0));
	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_add_tariff(&ctx, &pos, NULL, epoch, 0, 0));

	mid_session_free(&ctx);
}

TEST_CASE("Test persistence", "[mid]") {
	RESET;

	midlts_ctx_t ctx, ctx2;
	midlts_pos_t pos;

	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_init(&ctx, default_fw, default_lr));
	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_add_open(&ctx, &pos, NULL, epoch, 0, 0));
	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_add_tariff(&ctx, &pos, NULL, epoch, 0, 0));
	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_add_close(&ctx, &pos, NULL, epoch, 0, 0));
	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_add_tariff(&ctx, &pos, NULL, epoch, 0, 0));

	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_init(&ctx2, default_fw, default_lr));
	TEST_ASSERT_EQUAL_INT(ctx2.msg_id, ctx.msg_id);

	mid_session_free(&ctx);
	mid_session_free(&ctx2);
}

TEST_CASE("Test persistence over multiple pages", "[mid]") {
	RESET;

	midlts_ctx_t ctx, ctx1;
	midlts_pos_t pos;

	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_init(&ctx, default_fw, default_lr));

	for (size_t i = 0; i < 128; i++) {
		TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_add_tariff(&ctx, &pos, NULL, epoch, 0, 0));
	}

	mid_session_free(&ctx);
	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_init(&ctx, default_fw, default_lr));
	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_add_tariff(&ctx, &pos, NULL, epoch, 0, 0));

	TEST_ASSERT_EQUAL_INT(0, remove("/mid/0.ms"));

	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_init(&ctx1, default_fw, default_lr));
	TEST_ASSERT_EQUAL_INT(ctx.msg_id, ctx1.msg_id);

	mid_session_free(&ctx);
	mid_session_free(&ctx1);
}

TEST_CASE("Test reading records", "[mid]") {
	RESET;

	midlts_ctx_t ctx;
	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_init(&ctx, default_fw, default_lr));

	uint32_t flag = MID_SESSION_METER_VALUE_READING_FLAG_TARIFF;
	uint32_t meter = 0;
	uint32_t count = 0;

	midlts_pos_t pos[8];

	for (int i = 0; i < 8; i++) {
		TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_add_tariff(&ctx, &pos[i], NULL, epoch, flag, meter));
		meter += 100;
		count++;
	}

	mid_session_record_t rec;

	// Test bad reads
	midlts_pos_t badpos = pos[0];
	badpos.log_id++;
	TEST_ASSERT_NOT_EQUAL(LTS_OK, mid_session_read_record(&ctx, &badpos, &rec));
	badpos = pos[0];
	badpos.log_offset = 8 * 32;
	TEST_ASSERT_NOT_EQUAL(LTS_OK, mid_session_read_record(&ctx, &badpos, &rec));

	for (int i = 0; i < 8; i++) {
		// Test good read
		midlts_err_t e = mid_session_read_record(&ctx, &pos[i], &rec);
		TEST_ASSERT_EQUAL_INT(LTS_OK, e);
		TEST_ASSERT_EQUAL_INT(MID_SESSION_RECORD_TYPE_METER_VALUE, rec.rec_type);
	}

	mid_session_free(&ctx);
}

TEST_CASE("Test bad CRC returns an error", "[mid]") {
	RESET;

	uint64_t time = 0;

	midlts_ctx_t ctx;
	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_init(&ctx, default_fw, default_lr));

	uint32_t flag = MID_SESSION_METER_VALUE_READING_FLAG_TARIFF;
	uint32_t meter = 0;
	uint32_t count = 0;

	midlts_pos_t pos[8];

	for (int i = 0; i < 8; i++) {
		TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_add_tariff(&ctx, &pos[i], NULL, MID_TIME_TO_TS(time), flag, meter));
		meter += 100;
		count++;
		time += 1000 * 60 * 60;
	}

	// Corrupt first record
	FILE *fp = fopen("/mid/0.ms", "r+");
	TEST_ASSERT_NOT_EQUAL(NULL, fp);
	TEST_ASSERT(fputc('z', fp) == 'z');
	TEST_ASSERT(!fclose(fp));
	mid_session_free(&ctx);

	TEST_ASSERT_EQUAL_INT(LTS_BAD_CRC, mid_session_init(&ctx, default_fw, default_lr));
	mid_session_free(&ctx);
}

TEST_CASE("Test bad CRC returns an error last record", "[mid]") {
	RESET;

	uint64_t time = 0;

	midlts_ctx_t ctx;
	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_init(&ctx, default_fw, default_lr));

	uint32_t flag = MID_SESSION_METER_VALUE_READING_FLAG_TARIFF;
	uint32_t meter = 0;
	uint32_t count = 0;

	midlts_pos_t pos[8];

	for (int i = 0; i < 8; i++) {
		TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_add_tariff(&ctx, &pos[i], NULL, MID_TIME_TO_TS(time), flag, meter));
		meter += 100;
		count++;
		time += 1000 * 60 * 60;
	}

	FILE *fp = fopen("/mid/0.ms", "r+");
	TEST_ASSERT_NOT_EQUAL(NULL, fp)
	TEST_ASSERT_EQUAL_INT(0, fseek(fp, 32 * 7 + 16, SEEK_SET));
	TEST_ASSERT(fputc('z', fp) == 'z');
	TEST_ASSERT(!fclose(fp));
	mid_session_free(&ctx);

	TEST_ASSERT_EQUAL_INT(LTS_BAD_CRC, mid_session_init(&ctx, default_fw, default_lr));
	mid_session_free(&ctx);
}

TEST_CASE("Test position and reading", "[mid]") {
	RESET;

	uint64_t time = 0;

	midlts_ctx_t ctx;
	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_init(&ctx, default_fw, default_lr));

	uint32_t flag = MID_SESSION_METER_VALUE_READING_FLAG_TARIFF;
	uint32_t meter = 0;
	uint32_t count = 0;

	midlts_pos_t pos[8];

	for (int i = 0; i < 8; i++) {
		TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_add_tariff(&ctx, &pos[i], NULL, MID_TIME_TO_TS(time), flag, meter));
		meter += 100;
		count++;
		time += 1000 * 60 * 60;
	}

	for (int i = 0; i < 8; i++) {
		mid_session_record_t rec;
		TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_read_record(&ctx, &pos[i], &rec));
	}

	mid_session_free(&ctx);
}

TEST_CASE("Test purge fail", "[mid]") {
	RESET;

	uint64_t time = 0;

	midlts_ctx_t ctx;
	midlts_pos_t pos;
	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_init_internal(&ctx, 1, 2, default_fw, default_lr));

	uint32_t flag = MID_SESSION_METER_VALUE_READING_FLAG_TARIFF;
	uint32_t meter = 0;
	uint32_t count = 0;

	// Fill two full pages, next entry will try to delete the first page
	for (int i = 0; i < 128 * 2; i++) {
		TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_add_tariff(&ctx, &pos, NULL, MID_TIME_TO_TS(time), flag, meter));
		meter += 100;
		count++;
		time += 1000 * 60 * 60;
	}

	// Time will be epoch + 256, definitely not old enough to automatically purge
	TEST_ASSERT_NOT_EQUAL(LTS_OK, mid_session_add_tariff(&ctx, &pos, NULL, MID_TIME_TO_TS(time), flag, meter));

	mid_session_free(&ctx);
}

TEST_CASE("Test purge success", "[mid]") {
	RESET;

	midlts_ctx_t ctx;
	midlts_pos_t pos;

	uint64_t time = 0;

	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_init_internal(&ctx, 1, 2, default_fw, default_lr));

	uint32_t flag = MID_SESSION_METER_VALUE_READING_FLAG_TARIFF;
	uint32_t meter = 0;
	uint32_t count = 0;

	// Fill two full pages, next entry will try to delete the first page
	for (int i = 0; i < 128 * 2; i++) {
		TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_add_tariff(&ctx, &pos, NULL, MID_TIME_TO_TS(time), flag, meter));
		meter += 100;
		// Ensures first page is old enough at time of deletion
		time += 20925 * 1000;
		count++;
	}

	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_add_tariff(&ctx, &pos, NULL, MID_TIME_TO_TS(time), flag, meter));

	mid_session_free(&ctx);
}

TEST_CASE("Test purge failure - linked data", "[mid]") {
	RESET;

	midlts_ctx_t ctx;
	midlts_pos_t pos;

	uint64_t time = 0;

	TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_init_internal(&ctx, 1, 2, default_fw, default_lr));

	uint32_t flag = MID_SESSION_METER_VALUE_READING_FLAG_TARIFF;
	uint32_t meter = 0;
	uint32_t count = 0;

	// Fill two full pages, next entry will try to delete the first page
	for (int i = 0; i < 128 * 2; i++) {
		TEST_ASSERT_EQUAL_INT(LTS_OK, mid_session_add_tariff(&ctx, &pos, NULL, MID_TIME_TO_TS(time), flag, meter));
		if (i == 0) {
			mid_session_set_lts_purge_limit(&ctx, &pos);
		}

		meter += 100;
		// Ensures first page is old enough at time of deletion
		time += 20925 * 1000;
		count++;
	}

	TEST_ASSERT_NOT_EQUAL(LTS_OK, mid_session_add_tariff(&ctx, &pos, NULL, MID_TIME_TO_TS(time), flag, meter));

	mid_session_free(&ctx);
}

