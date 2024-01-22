#include <time.h>
#include <limits.h>
#include "unity.h"
#include "esp_log.h"
#include "mid_lts.h"
#include "mid_lts_test.h"

static const char *TAG = "MIDTEST";

#define RESET TEST_ASSERT(mid_session_reset() == LTS_OK)

static const mid_session_version_fw_t default_fw = { 2, 0, 4, 201 };
static const mid_session_version_lr_t default_lr = { 1, 2, 3 };

TEST_CASE("Test no leakages", "[mid]") {
	RESET;

	midlts_ctx_t ctx;
	TEST_ASSERT(mid_session_init(&ctx, 0, default_fw, default_lr) == LTS_OK);
}

TEST_CASE("Test session closed when not open", "[mid]") {
	RESET;

	midlts_ctx_t ctx;
	midlts_pos_t pos;

	TEST_ASSERT(mid_session_init(&ctx, 0, default_fw, default_lr) == LTS_OK);
	TEST_ASSERT(mid_session_add_close(&ctx, &pos, 0, 0, 0) == LTS_SESSION_NOT_OPEN);
	TEST_ASSERT(MID_SESSION_IS_CLOSED(&ctx));
}

TEST_CASE("Test session open close", "[mid][allowleak]") {
	RESET;

	midlts_ctx_t ctx;
	midlts_pos_t pos;

	uint8_t uuid[16] = {0};

	TEST_ASSERT(mid_session_init(&ctx, 0, default_fw, default_lr) == LTS_OK);
	TEST_ASSERT(mid_session_add_open(&ctx, &pos, 0, uuid, 0, 0) == LTS_OK);
	TEST_ASSERT(MID_SESSION_IS_OPEN(&ctx));
	TEST_ASSERT(mid_session_add_close(&ctx, &pos, 0, 0, 0) == LTS_OK);
	TEST_ASSERT(MID_SESSION_IS_CLOSED(&ctx));

	// TODO: Readback with pos and verify
}

TEST_CASE("Test session flag persistence", "[mid]") {
	RESET;

	midlts_ctx_t ctx;
	midlts_pos_t pos;

	uint8_t uuid[16] = {0};

	TEST_ASSERT(mid_session_init(&ctx, 0, default_fw, default_lr) == LTS_OK);
	TEST_ASSERT(mid_session_add_open(&ctx, &pos, 0, uuid, 0, 0) == LTS_OK);

	TEST_ASSERT(mid_session_init(&ctx, 0, default_fw, default_lr) == LTS_OK);
	TEST_ASSERT(MID_SESSION_IS_OPEN(&ctx));

	TEST_ASSERT(mid_session_add_close(&ctx, &pos, 0, 0, 0) == LTS_OK);
	TEST_ASSERT(MID_SESSION_IS_CLOSED(&ctx));

	TEST_ASSERT(mid_session_init(&ctx, 0, default_fw, default_lr) == LTS_OK);
	TEST_ASSERT(MID_SESSION_IS_CLOSED(&ctx));
}

TEST_CASE("Test session open twice", "[mid]") {
	RESET;

	midlts_ctx_t ctx;
	midlts_pos_t pos;

	uint8_t uuid[16] = {0};

	TEST_ASSERT(mid_session_init(&ctx, 0, default_fw, default_lr) == LTS_OK);
	TEST_ASSERT(mid_session_add_open(&ctx, &pos, 0, uuid, 0, 0) == LTS_OK);
	TEST_ASSERT(mid_session_add_open(&ctx, &pos, 0, uuid, 0, 0) == LTS_SESSION_ALREADY_OPEN);
	TEST_ASSERT(mid_session_add_close(&ctx, &pos, 0, 0, 0) == LTS_OK);
}

TEST_CASE("Test tariff change allowed out of session", "[mid]") {
	RESET;

	midlts_ctx_t ctx;
	midlts_pos_t pos;

	uint8_t uuid[16] = {0};

	TEST_ASSERT(mid_session_init(&ctx, 0, default_fw, default_lr) == LTS_OK);
	TEST_ASSERT(mid_session_add_open(&ctx, &pos, 0, uuid, 0, 0) == LTS_OK);
	TEST_ASSERT(mid_session_add_tariff(&ctx, &pos, 0, 0, 0) == LTS_OK);
	TEST_ASSERT(mid_session_add_close(&ctx, &pos, 0, 0, 0) == LTS_OK);
	TEST_ASSERT(mid_session_add_tariff(&ctx, &pos, 0, 0, 0) == LTS_OK);
}

TEST_CASE("Test persistence", "[mid]") {
	RESET;

	midlts_ctx_t ctx;
	midlts_pos_t pos;

	uint8_t uuid[16] = {0};

	TEST_ASSERT(mid_session_init(&ctx, 0, default_fw, default_lr) == LTS_OK);
	TEST_ASSERT(mid_session_add_open(&ctx, &pos, 0, uuid, 0, 0) == LTS_OK);
	TEST_ASSERT(mid_session_add_tariff(&ctx, &pos, 0, 0, 0) == LTS_OK);
	TEST_ASSERT(mid_session_add_close(&ctx, &pos, 0, 0, 0) == LTS_OK);
	TEST_ASSERT(mid_session_add_tariff(&ctx, &pos, 0, 0, 0) == LTS_OK);

	midlts_ctx_t ctx2;
	TEST_ASSERT(mid_session_init(&ctx2, 0, default_fw, default_lr) == LTS_OK);
	TEST_ASSERT(memcmp(&ctx, &ctx2, sizeof (ctx)) == 0);
}

TEST_CASE("Test persistence over multiple pages", "[mid]") {
	RESET;

	midlts_ctx_t ctx;
	midlts_pos_t pos;

	TEST_ASSERT(mid_session_init(&ctx, 0, default_fw, default_lr) == LTS_OK);

	for (size_t i = 0; i < 128; i++) {
		TEST_ASSERT(mid_session_add_tariff(&ctx, &pos, 0, 0, 0) == LTS_OK);
	}

	TEST_ASSERT(mid_session_init(&ctx, 0, default_fw, default_lr) == LTS_OK);
	TEST_ASSERT(mid_session_add_tariff(&ctx, &pos, 0, 0, 0) == LTS_OK);

	TEST_ASSERT(remove("/mid/0.ms") == 0);

	midlts_ctx_t ctx1;
	TEST_ASSERT(mid_session_init(&ctx1, 0, default_fw, default_lr) == LTS_OK);
	TEST_ASSERT(memcmp(&ctx, &ctx1, sizeof (ctx)) == 0);
}

TEST_CASE("Test wraparound", "[mid]") {
	RESET;

	midlts_ctx_t ctx;
	midlts_pos_t pos;

	// Max 4 pages
	TEST_ASSERT(mid_session_init_internal(&ctx, 4, 0, default_fw, default_lr) == LTS_OK);

	uint32_t time = MID_EPOCH;
	uint32_t flag = MID_SESSION_METER_VALUE_READING_FLAG_TARIFF;
	uint32_t meter = 0;
	uint32_t count = 0;

	for (int i = 0; i < 128 * 4 + 1; i++) {
		TEST_ASSERT(mid_session_add_tariff(&ctx, &pos, time++, flag, meter) == LTS_OK);
		meter += 100;
		count++;
	}


	midlts_ctx_t ctx0;
	TEST_ASSERT_EQUAL_INT(mid_session_init_internal(&ctx0, 4, 0, default_fw, default_lr), LTS_OK);
	TEST_ASSERT(memcmp(&ctx0, &ctx, sizeof (ctx)) == 0);

	// Should be around a bit over a wraparound for 4 files so 0 should be enough
	TEST_ASSERT(remove("/mid/1.ms") == 0);
	TEST_ASSERT(remove("/mid/2.ms") == 0);
	TEST_ASSERT(remove("/mid/3.ms") == 0);

	TEST_ASSERT(mid_session_init_internal(&ctx0, 4, 0, default_fw, default_lr) == LTS_OK);
	TEST_ASSERT(memcmp(&ctx0, &ctx, sizeof (ctx)) == 0);
}

TEST_CASE("Test reading records", "[mid]") {
	RESET;

	midlts_ctx_t ctx;
	TEST_ASSERT(mid_session_init(&ctx, 0, default_fw, default_lr) == LTS_OK);

	uint32_t time = MID_EPOCH;
	uint32_t flag = MID_SESSION_METER_VALUE_READING_FLAG_TARIFF;
	uint32_t meter = 0;
	uint32_t count = 0;

	midlts_pos_t pos[8];

	for (int i = 0; i < 8; i++) {
		TEST_ASSERT(mid_session_add_tariff(&ctx, &pos[i], time++, flag, meter) == LTS_OK);
		meter += 100;
		count++;
	}

	mid_session_record_t rec;

	// Test bad reads
	midlts_pos_t badpos = pos[0];
	badpos.id++;
	TEST_ASSERT(mid_session_read_record(&ctx, &badpos, &rec) != LTS_OK);
	badpos = pos[0];
	badpos.offset = 8 * 32;
	TEST_ASSERT(mid_session_read_record(&ctx, &badpos, &rec) != LTS_OK);
	badpos = pos[0];
	badpos.crc = 0xffffffff;
	TEST_ASSERT(mid_session_read_record(&ctx, &badpos, &rec) != LTS_OK);

	for (int i = 0; i < 8; i++) {
		// Test good read
		midlts_err_t e = mid_session_read_record(&ctx, &pos[i], &rec);
		TEST_ASSERT(e == LTS_OK);
		TEST_ASSERT(rec.rec_type == MID_SESSION_RECORD_TYPE_METER_VALUE);
	}
}

TEST_CASE("Test bad CRC returns an error", "[mid]") {
	RESET;

	midlts_ctx_t ctx;
	TEST_ASSERT(mid_session_init(&ctx, 0, default_fw, default_lr) == LTS_OK);

	uint32_t time = 0;
	uint32_t flag = MID_SESSION_METER_VALUE_READING_FLAG_TARIFF;
	uint32_t meter = 0;
	uint32_t count = 0;

	midlts_pos_t pos[8];

	for (int i = 0; i < 8; i++) {
		TEST_ASSERT(mid_session_add_tariff(&ctx, &pos[i], time++, flag, meter) == LTS_OK);
		meter += 100;
		count++;
	}

	// Corrupt first record
	FILE *fp = fopen("/mid/0.ms", "r+");
	TEST_ASSERT(fp != NULL);
	TEST_ASSERT(fputc('z', fp) == 'z');
	TEST_ASSERT(!fclose(fp));

	TEST_ASSERT(mid_session_init(&ctx, 0, default_fw, default_lr) == LTS_BAD_CRC);
}

TEST_CASE("Test bad CRC returns an error last record", "[mid]") {
	RESET;

	midlts_ctx_t ctx;
	TEST_ASSERT(mid_session_init(&ctx, 0, default_fw, default_lr) == LTS_OK);

	uint32_t time = 0;
	uint32_t flag = MID_SESSION_METER_VALUE_READING_FLAG_TARIFF;
	uint32_t meter = 0;
	uint32_t count = 0;

	midlts_pos_t pos[8];

	for (int i = 0; i < 8; i++) {
		TEST_ASSERT(mid_session_add_tariff(&ctx, &pos[i], time++, flag, meter) == LTS_OK);
		meter += 100;
		count++;
	}

	FILE *fp = fopen("/mid/0.ms", "r+");
	TEST_ASSERT(fp != NULL);
	TEST_ASSERT(fseek(fp, 32 * 7 + 16, SEEK_SET) == 0);
	TEST_ASSERT(fputc('z', fp) == 'z');
	TEST_ASSERT(!fclose(fp));

	TEST_ASSERT(mid_session_init(&ctx, 0, default_fw, default_lr) == LTS_BAD_CRC);
}

TEST_CASE("Test position and reading", "[mid]") {
	RESET;

	midlts_ctx_t ctx;
	TEST_ASSERT(mid_session_init(&ctx, 0, default_fw, default_lr) == LTS_OK);

	uint32_t time = MID_EPOCH;
	uint32_t flag = MID_SESSION_METER_VALUE_READING_FLAG_TARIFF;
	uint32_t meter = 0;
	uint32_t count = 0;

	midlts_pos_t pos[8];

	for (int i = 0; i < 8; i++) {
		TEST_ASSERT(mid_session_add_tariff(&ctx, &pos[i], time++, flag, meter) == LTS_OK);
		meter += 100;
		count++;
	}

	for (int i = 0; i < 8; i++) {
		mid_session_record_t rec;
		TEST_ASSERT(mid_session_read_record(&ctx, &pos[i], &rec) == LTS_OK);
	}
}
