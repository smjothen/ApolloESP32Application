#include <time.h>
#include <limits.h>
#include "unity.h"
#include "esp_log.h"
#include "mid_lts.h"

TEST_CASE("Test no leakages", "[mid]") {
	midlts_ctx_t ctx;
	TEST_ASSERT(mid_session_init(&ctx, 0, "2.0.4.1", "v1.2.3") == LTS_OK);
}
