#include <time.h>
#include <limits.h>
#include "unity.h"
#include "esp_log.h"
#include "mid_lts.h"
#include "mid_lts_test.h"

static const char *TAG = "MIDTEST";

#define RESET TEST_ASSERT(mid_session_reset() == LTS_OK)

TEST_CASE("Test no leakages", "[mid]") {
	RESET;

	midlts_ctx_t ctx;
	TEST_ASSERT(mid_session_init(&ctx, 0, "2.0.4.1", "v1.2.3") == LTS_OK);
}

TEST_CASE("Test session closed when not open", "[mid]") {
	RESET;

	midlts_ctx_t ctx;
	midlts_pos_t pos;

	TEST_ASSERT(mid_session_init(&ctx, 0, "2.0.4.1", "v1.2.3") == LTS_OK);
	TEST_ASSERT(mid_session_add_close(&ctx, &pos, 0, 0, 0) == LTS_SESSION_NOT_OPEN);
	TEST_ASSERT(MID_SESSION_IS_CLOSED(&ctx));
}

TEST_CASE("Test session open close", "[mid]") {
	RESET;

	midlts_ctx_t ctx;
	midlts_pos_t pos;

	uint8_t uuid[16] = {0};

	TEST_ASSERT(mid_session_init(&ctx, 0, "2.0.4.1", "v1.2.3") == LTS_OK);
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

	TEST_ASSERT(mid_session_init(&ctx, 0, "2.0.4.1", "v1.2.3") == LTS_OK);
	TEST_ASSERT(mid_session_add_open(&ctx, &pos, 0, uuid, 0, 0) == LTS_OK);

	TEST_ASSERT(mid_session_init(&ctx, 0, "2.0.4.1", "v1.2.3") == LTS_OK);
	TEST_ASSERT(MID_SESSION_IS_OPEN(&ctx));

	TEST_ASSERT(mid_session_add_close(&ctx, &pos, 0, 0, 0) == LTS_OK);
	TEST_ASSERT(MID_SESSION_IS_CLOSED(&ctx));

	TEST_ASSERT(mid_session_init(&ctx, 0, "2.0.4.1", "v1.2.3") == LTS_OK);
	TEST_ASSERT(MID_SESSION_IS_CLOSED(&ctx));
}

TEST_CASE("Test session open twice", "[mid]") {
	RESET;

	midlts_ctx_t ctx;
	midlts_pos_t pos;

	uint8_t uuid[16] = {0};

	TEST_ASSERT(mid_session_init(&ctx, 0, "2.0.4.1", "v1.2.3") == LTS_OK);
	TEST_ASSERT(mid_session_add_open(&ctx, &pos, 0, uuid, 0, 0) == LTS_OK);
	TEST_ASSERT(mid_session_add_open(&ctx, &pos, 0, uuid, 0, 0) == LTS_SESSION_ALREADY_OPEN);
	TEST_ASSERT(mid_session_add_close(&ctx, &pos, 0, 0, 0) == LTS_OK);
}

TEST_CASE("Test tariff change allowed out of session", "[mid]") {
	RESET;

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
	RESET;

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
	RESET;

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

	TEST_ASSERT(strcmp(ctx3.latest_fw, "2.0.4.2") == 0);
	TEST_ASSERT(strcmp(ctx3.latest_lr, "v1.2.4") == 0);
}

/*
TEST_CASE("Test persistence over multiple pages", "[mid]") {
	RESET;

	midlts_ctx_t ctx;

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

	TEST_ASSERT(mid_session_init(&ctx, 0, "2.0.4.2", "v1.2.3") == LTS_OK);
	TEST_ASSERT(mid_session_add_tariff(&ctx, &pos[0], 0, 0, 0) == LTS_OK);

	TEST_ASSERT(mid_session_get_record(&pos[0], &rec) == LTS_OK);
	TEST_ASSERT(rec.has_meter_value && rec.has_fw_version && !rec.has_lr_version && strcmp(rec.fw_version.code, "2.0.4.2") == 0);

	TEST_ASSERT(mid_session_init(&ctx, 0, "2.0.4.2", "v1.2.4") == LTS_OK);
	TEST_ASSERT(mid_session_add_tariff(&ctx, &pos[0], 0, 0, 0) == LTS_OK);

	TEST_ASSERT(mid_session_get_record(&pos[0], &rec) == LTS_OK);
	TEST_ASSERT(rec.has_meter_value && !rec.has_fw_version && rec.has_lr_version && strcmp(rec.lr_version.code, "v1.2.4") == 0);
}

TEST_CASE("Test multifile", "[mid]") {
	FORMAT;
	midlts_ctx_t ctx;
	midlts_pos_t pos;

	TEST_ASSERT(mid_session_init(&ctx, 0, "2.0.4.1", "v1.2.3") == LTS_OK);

	uint32_t time = 0;
	uint32_t flag = MID_SESSION_METER_VALUE_READING_FLAG_TARIFF;
	uint32_t meter = 0;

	uint32_t count = 0;

	while (true) {
		TEST_ASSERT(mid_session_add_tariff(&ctx, &pos, time++, flag, meter) == LTS_OK);
		meter += 100;
		count++;
		if (pos.id > 0) {
			break;
		}
	}

	TEST_ASSERT(pos.id == 1);
	TEST_ASSERT(pos.off == 0);

	mid_session_record_t rec;
	TEST_ASSERT(mid_session_get_record(&pos, &rec) == LTS_OK);
	TEST_ASSERT(rec.has_fw_version && rec.has_lr_version && rec.has_meter_value);

	TEST_ASSERT(mid_session_add_tariff(&ctx, &pos, time++, flag, meter) == LTS_OK);
	count++;

	TEST_ASSERT(pos.id == 1);
	TEST_ASSERT(pos.off > 0);

	TEST_ASSERT(mid_session_get_record(&pos, &rec) == LTS_OK);
	TEST_ASSERT(!rec.has_fw_version && !rec.has_lr_version && rec.has_meter_value);

	pos = MIDLTS_POS_MIN;

	uint32_t count2 = 0;
	midlts_err_t err;
	while ((err = mid_session_get_record(&pos, &rec)) == LTS_OK) {
		count2++;
	}
	ESP_LOGI(TAG, "%" PRIu32 " %" PRIu32, count, count2);

	TEST_ASSERT(err == LTS_EOF);
	TEST_ASSERT(count == count2);

	TEST_ASSERT(mid_session_init(&ctx, 0, "2.0.4.5", "v1.2.4") == LTS_OK);

	TEST_ASSERT(mid_session_add_tariff(&ctx, &pos, time++, flag, meter) == LTS_OK);
	TEST_ASSERT(mid_session_get_record(&pos, &rec) == LTS_OK);
	TEST_ASSERT(rec.has_fw_version && rec.has_lr_version && rec.has_meter_value);
}
*/
