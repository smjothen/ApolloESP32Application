#include <time.h>
#include <limits.h>
#include "unity.h"
#include "esp_log.h"
#include "mid_lts.h"
#include "mid_lts_test.h"

static const char *TAG = "MIDTEST";

#define RESET TEST_ASSERT(mid_session_reset() == LTS_OK)
#define RESETPAGE(n) TEST_ASSERT(mid_session_reset_page((n)) == LTS_OK)

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

TEST_CASE("Test persistence over multiple pages", "[mid]") {
	RESET;

	midlts_ctx_t ctx;
	midlts_pos_t pos;

	uint8_t uuid[16] = {0};
	TEST_ASSERT(mid_session_init(&ctx, 0, "2.0.4.1", "v1.2.3") == LTS_OK);

	// The 2 first records are LR/FW version records, so this leaves 1
	// spot left to program
	for (size_t i = 0; i < 128 - 3; i++) {
		TEST_ASSERT(mid_session_add_tariff(&ctx, &pos, 0, 0, 0) == LTS_OK);
	}

	TEST_ASSERT(mid_session_init(&ctx, 0, "2.0.4.2", "v1.2.4") == LTS_OK);
	TEST_ASSERT(mid_session_add_tariff(&ctx, &pos, 0, 0, 0) == LTS_OK);

	RESETPAGE(0);

	TEST_ASSERT(mid_session_init(&ctx, 0, "2.0.4.2", "v1.2.4") == LTS_OK);

	TEST_ASSERT(strcmp(ctx.latest_lr, "v1.2.4") == 0);
	TEST_ASSERT(strcmp(ctx.latest_fw, "2.0.4.2") == 0);
}

TEST_CASE("Test wraparound", "[mid]") {
	RESET;

	midlts_ctx_t ctx;
	midlts_pos_t pos;

	TEST_ASSERT(mid_session_init(&ctx, 0, "2.0.4.1", "v1.2.3") == LTS_OK);

	uint32_t time = 0;
	uint32_t flag = MID_SESSION_METER_VALUE_READING_FLAG_TARIFF;
	uint32_t meter = 0;

	uint32_t count = 0;

	for (int i = 0; i < 128 * 4; i++) {
		TEST_ASSERT(mid_session_add_tariff(&ctx, &pos, time++, flag, meter) == LTS_OK);
		meter += 100;
		count++;
	}

	// Should be around a bit over a wraparound

	midlts_ctx_t ctx0;
	TEST_ASSERT(mid_session_init(&ctx0, 0, "2.0.4.1", "v1.2.3") == LTS_OK);
	TEST_ASSERT(memcmp(&ctx0, &ctx, sizeof (ctx)) == 0);

	RESETPAGE(1);
	RESETPAGE(2);
	RESETPAGE(3);

	TEST_ASSERT(mid_session_init(&ctx0, 0, "2.0.4.1", "v1.2.3") == LTS_OK);
	TEST_ASSERT(memcmp(&ctx0, &ctx, sizeof (ctx)) == 0);
}

TEST_CASE("Test reading records", "[mid]") {
	RESET;

	midlts_ctx_t ctx;
	TEST_ASSERT(mid_session_init(&ctx, 0, "2.0.4.1", "v1.2.3") == LTS_OK);

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

	// Test bad read
	mid_session_record_t rec;
	TEST_ASSERT(mid_session_read_record(&ctx, &(midlts_pos_t) { .loc = 1, .id = 0 }, &rec) != LTS_OK);

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
	TEST_ASSERT(mid_session_init(&ctx, 0, "2.0.4.1", "v1.2.3") == LTS_OK);

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
	uint8_t rec[sizeof (mid_session_record_t)];
	TEST_ASSERT(esp_partition_read(ctx.partition, 0, rec, sizeof (rec)) == ESP_OK);

	ESP_LOG_BUFFER_HEX(TAG, rec, sizeof (rec));
	for (size_t i = 0; i < sizeof (mid_session_record_t); i++) {
		if (rec[i] != 0) {
			rec[i] = 0;
			break;
		}
	}
	ESP_LOG_BUFFER_HEX(TAG, rec, sizeof (rec));

	TEST_ASSERT(esp_partition_write(ctx.partition, 0, rec, sizeof (rec)) == ESP_OK);
	TEST_ASSERT(mid_session_init(&ctx, 0, "2.0.4.1", "v1.2.3") != LTS_OK);
}

TEST_CASE("Test bad CRC returns an error last record", "[mid]") {
	RESET;

	midlts_ctx_t ctx;
	TEST_ASSERT(mid_session_init(&ctx, 0, "2.0.4.1", "v1.2.3") == LTS_OK);

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

	// Corrupt last (2 + 8) == 10th record
	uint8_t rec[sizeof (mid_session_record_t)];
	TEST_ASSERT(esp_partition_read(ctx.partition, 9 * 32, rec, sizeof (rec)) == ESP_OK);

	ESP_LOG_BUFFER_HEX(TAG, rec, sizeof (rec));
	for (size_t i = 0; i < sizeof (mid_session_record_t); i++) {
		if (rec[i] != 0) {
			rec[i] = 0;
			break;
		}
	}
	ESP_LOG_BUFFER_HEX(TAG, rec, sizeof (rec));

	TEST_ASSERT(esp_partition_write(ctx.partition, 0, rec, sizeof (rec)) == ESP_OK);
	TEST_ASSERT(mid_session_init(&ctx, 0, "2.0.4.1", "v1.2.3") != LTS_OK);
}


