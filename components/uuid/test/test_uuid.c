#include <time.h>
#include <limits.h>
#include <string.h>

#include "unity.h"
#include "esp_log.h"

#include "uuid.h"

static const char *TAG = "UUIDTEST";

TEST_CASE("Test UUID 1", "[uuid]") {
	uuid_t uuid = uuid_generate();

	char buf[37];
	TEST_ASSERT(uuid_to_string(uuid, buf, sizeof (buf)));

	ESP_LOGI(TAG, "%s", buf);

	uuid_t uuid2;
	TEST_ASSERT(uuid_from_string(&uuid2, buf));

	ESP_LOG_BUFFER_HEX(TAG, &uuid, sizeof (uuid));
	ESP_LOG_BUFFER_HEX(TAG, &uuid2, sizeof (uuid));

	TEST_ASSERT(memcmp(&uuid, &uuid2, sizeof (uuid)) == 0);
}

TEST_CASE("Test UUID 2", "[uuid]") {
	uuid_t uuid = {1,2,3,4,5,{6}};
	TEST_ASSERT(uuid_from_string(&uuid, "bd66f074-54b4-429c-b632-70f45927b187"));
	char buf[37];
	TEST_ASSERT(uuid_to_string(uuid, buf, sizeof (buf)));
	TEST_ASSERT_EQUAL_STRING("bd66f074-54b4-429c-b632-70f45927b187", buf);
}

TEST_CASE("Test Bad UUID", "[uuid]") {
	uuid_t uuid;
	TEST_ASSERT(!uuid_from_string(&uuid, "bd66f074-54z4-429c-b632-70f45927b187"));
	TEST_ASSERT(!uuid_from_string(&uuid, "bd66f074-54b4a429c-b632-70f45927b187"));
	TEST_ASSERT(!uuid_from_string(&uuid, "bd66f074-54b4x429c-b632-70f45927b187"));
	TEST_ASSERT(!uuid_from_string(&uuid, "bd66f074-54b4x429c-b632-70f45927b18"));
	TEST_ASSERT(!uuid_from_string(&uuid, ""));
	TEST_ASSERT(!uuid_from_string(&uuid, "ab"));
	TEST_ASSERT(!uuid_from_string(&uuid, "bd66f074-54z4-429c-b632-70f45927b187"));
	TEST_ASSERT(!uuid_from_string(&uuid, "bd66f074-54z4-429c-b632-70f45927b187"));
	TEST_ASSERT(!uuid_from_string(&uuid, "bd66f074-54z4-429c-b632-70f45927b187a"));
	TEST_ASSERT(!uuid_from_string(&uuid, "bd66f074-54z4-429c-b632-70f45927b187ab"));
	TEST_ASSERT(!uuid_from_string(&uuid, "bd66f074-54z4-429c-b632-70f45927b187abc"));
}

TEST_CASE("Test Good UUID", "[uuid]") {
	uuid_t uuid;
	TEST_ASSERT(uuid_from_string(&uuid, "bd66f074-54b4-429c-b632-70f45927b187"));
	TEST_ASSERT(uuid_from_string(&uuid, "00000000-0000-0000-0000-000000000000"));
	TEST_ASSERT(uuid_from_string(&uuid, "ffffffff-ffff-ffff-ffff-ffffffffffff"));
}

TEST_CASE("Test packing", "[uuid]") {
	uint8_t bytes[16] = {0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf1, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x12};

	uuid_t id;
	uuid_from_bytes(&id, bytes);

	uint8_t bytes2[16];
	uuid_to_bytes(id, bytes2);

	TEST_ASSERT(memcmp(bytes, bytes2, sizeof (bytes)) == 0);

	char buf[37];
	TEST_ASSERT(uuid_to_string(id, buf, sizeof (buf)));
	TEST_ASSERT_EQUAL_STRING("12345678-9abc-def1-2345-6789abcdef12", buf);
}
