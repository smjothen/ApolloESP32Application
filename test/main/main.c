#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "unity.h"
#include "unity_test_runner.h"
#include "unity_test_utils_memory.h"
#include "esp_log.h"
#ifdef CONFIG_HEAP_TRACING
#include "memory_checks.h"
#include "esp_heap_trace.h"
#endif

#define TEST_PARTITION "/files"

const char *TAG = "UNITTEST";

void setUp(void) {
	// If heap tracing is enabled in kconfig, leak trace the test
#ifdef CONFIG_HEAP_TRACING
	setup_heap_record();
	heap_trace_start(HEAP_TRACE_LEAKS);
#endif

	unity_utils_set_leak_level(0);
	unity_utils_record_free_mem();
}

void tearDown(void) {
#ifdef CONFIG_HEAP_TRACING
	heap_trace_stop();
	heap_trace_dump();
#endif

	unity_utils_evaluate_leaks();
}

void app_main(void) {
	wl_handle_t s_wl_handle = WL_INVALID_HANDLE;

	esp_vfs_fat_mount_config_t mount_config = {
		.max_files = 5,
		.format_if_mount_failed = true,
		.allocation_unit_size = CONFIG_WL_SECTOR_SIZE
	};

	TEST_ESP_OK(esp_vfs_fat_spiflash_mount_rw_wl(TEST_PARTITION, NULL, &mount_config, &s_wl_handle));

	// NB: ESP-IDF first printf does some allocations, so to not cause false positives in tests
	// just leave this here.
	printf("%s", "");

	UNITY_BEGIN();
	unity_run_all_tests();
	UNITY_END();

	unity_run_menu();

	TEST_ESP_OK(esp_vfs_fat_spiflash_unmount_rw_wl(TEST_PARTITION, s_wl_handle));
}
