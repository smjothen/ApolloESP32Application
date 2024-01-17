#include <time.h>
#include <limits.h>
#include "unity.h"
#include "esp_log.h"
#include "esp_littlefs.h"
#include "mid_lts.h"

static const char *TAG = "MIDTEST";

#define FORMAT TEST_ASSERT(esp_littlefs_format("mid") == ESP_OK)

TEST_CASE("Test no leakages", "[mid]") {
	FORMAT;

	midlts_ctx_t ctx;
	TEST_ASSERT(mid_session_init(&ctx, 0, "2.0.4.1", "v1.2.3") == LTS_OK);
}

TEST_CASE("Test session closed when not open", "[mid]") {
	FORMAT;

	midlts_ctx_t ctx;
	midlts_pos_t pos;

	TEST_ASSERT(mid_session_init(&ctx, 0, "2.0.4.1", "v1.2.3") == LTS_OK);
	TEST_ASSERT(mid_session_add_close(&ctx, &pos, 0, 0, 0) == LTS_SESSION_NOT_OPEN);
}

TEST_CASE("Test session open close", "[mid]") {
	FORMAT;

	midlts_ctx_t ctx;
	midlts_pos_t pos;

	uint8_t uuid[16] = {0};

	TEST_ASSERT(mid_session_init(&ctx, 0, "2.0.4.1", "v1.2.3") == LTS_OK);
	TEST_ASSERT(mid_session_add_open(&ctx, &pos, 0, uuid, 0, 0) == LTS_OK);
	TEST_ASSERT(mid_session_add_close(&ctx, &pos, 0, 0, 0) == LTS_OK);

	// TODO: Readback with pos and verify
}

TEST_CASE("Test session open twice", "[mid]") {
	FORMAT;

	midlts_ctx_t ctx;
	midlts_pos_t pos;

	uint8_t uuid[16] = {0};

	TEST_ASSERT(mid_session_init(&ctx, 0, "2.0.4.1", "v1.2.3") == LTS_OK);
	TEST_ASSERT(mid_session_add_open(&ctx, &pos, 0, uuid, 0, 0) == LTS_OK);
	TEST_ASSERT(mid_session_add_open(&ctx, &pos, 0, uuid, 0, 0) == LTS_SESSION_ALREADY_OPEN);
	TEST_ASSERT(mid_session_add_close(&ctx, &pos, 0, 0, 0) == LTS_OK);
}

TEST_CASE("Test tariff change allowed out of session", "[mid]") {
	FORMAT;

	midlts_ctx_t ctx;
	midlts_pos_t pos;

	uint8_t uuid[16] = {0};

	TEST_ASSERT(mid_session_init(&ctx, 0, "2.0.4.1", "v1.2.3") == LTS_OK);
	TEST_ASSERT(mid_session_add_open(&ctx, &pos, 0, uuid, 0, 0) == LTS_OK);
	TEST_ASSERT(mid_session_add_tariff(&ctx, &pos, 0, 0, 0) == LTS_OK);
	TEST_ASSERT(mid_session_add_close(&ctx, &pos, 0, 0, 0) == LTS_OK);
	TEST_ASSERT(mid_session_add_tariff(&ctx, &pos, 0, 0, 0) == LTS_OK);
}


TEST_CASE("Test persistence", "[mid]") {
	FORMAT;

	midlts_ctx_t ctx;
	midlts_pos_t pos;

	uint8_t uuid[16] = {0};

	TEST_ASSERT(mid_session_init(&ctx, 0, "2.0.4.1", "v1.2.3") == LTS_OK);
	TEST_ASSERT(mid_session_add_open(&ctx, &pos, 0, uuid, 0, 0) == LTS_OK);
	TEST_ASSERT(mid_session_add_tariff(&ctx, &pos, 0, 0, 0) == LTS_OK);
	TEST_ASSERT(mid_session_add_close(&ctx, &pos, 0, 0, 0) == LTS_OK);
	TEST_ASSERT(mid_session_add_tariff(&ctx, &pos, 0, 0, 0) == LTS_OK);

	midlts_ctx_t ctx2;
	TEST_ASSERT(mid_session_init(&ctx2, 0, "2.0.4.1", "v1.2.3") == LTS_OK);
	TEST_ASSERT(memcmp(&ctx, &ctx2, sizeof (ctx)) == 0);
}

TEST_CASE("Test latest version persists", "[mid]") {
	FORMAT;

	midlts_ctx_t ctx, ctx2, ctx3;
	midlts_pos_t pos;

	uint8_t uuid[16] = {0};

	TEST_ASSERT(mid_session_init(&ctx, 0, "2.0.4.1", "v1.2.3") == LTS_OK);
	TEST_ASSERT(mid_session_add_open(&ctx, &pos, 0, uuid, 0, 0) == LTS_OK);
	TEST_ASSERT(mid_session_add_tariff(&ctx, &pos, 0, 0, 0) == LTS_OK);
	TEST_ASSERT(mid_session_add_close(&ctx, &pos, 0, 0, 0) == LTS_OK);

	TEST_ASSERT(mid_session_init(&ctx2, 0, "2.0.4.2", "v1.2.4") == LTS_OK);
	TEST_ASSERT(mid_session_add_tariff(&ctx2, &pos, 0, 0, 0) == LTS_OK);

	TEST_ASSERT(mid_session_init(&ctx3, 0, "2.0.4.2", "v1.2.4") == LTS_OK);

	TEST_ASSERT(strcmp(ctx3.current_file.fw_version.code, "2.0.4.2") == 0);
	TEST_ASSERT(strcmp(ctx3.current_file.lr_version.code, "v1.2.4") == 0);
}

TEST_CASE("Test reading of records", "[mid]") {
	FORMAT;

	midlts_ctx_t ctx;
	midlts_pos_t pos[3];

	uint8_t uuid[16] = {0};

	TEST_ASSERT(mid_session_init(&ctx, 0, "2.0.4.1", "v1.2.3") == LTS_OK);
	TEST_ASSERT(mid_session_add_open(&ctx, &pos[0], 0, uuid, 0, 0) == LTS_OK);
	TEST_ASSERT(mid_session_add_tariff(&ctx, &pos[1], 0, 0, 0) == LTS_OK);
	TEST_ASSERT(mid_session_add_close(&ctx, &pos[2], 0, 0, 0) == LTS_OK);

	mid_session_record_t rec;
	midlts_pos_t iter = pos[0];

	TEST_ASSERT(mid_session_get_record(&iter, &rec) == LTS_OK);
	TEST_ASSERT(rec.has_fw_version && rec.has_lr_version && rec.has_id && rec.has_meter_value && (rec.meter_value.flag & MID_SESSION_METER_VALUE_READING_FLAG_START));

	TEST_ASSERT(mid_session_get_record(&iter, &rec) == LTS_OK);
	TEST_ASSERT(rec.has_meter_value);

	TEST_ASSERT(mid_session_get_record(&iter, &rec) == LTS_OK);
	TEST_ASSERT(rec.has_meter_value);

	TEST_ASSERT(mid_session_get_record(&iter, &rec) == LTS_EOF);
}
