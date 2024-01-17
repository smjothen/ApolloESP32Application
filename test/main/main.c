#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "unity.h"
#include "unity_test_runner.h"
#include "unity_test_utils_memory.h"
#include "esp_log.h"
#include "esp_littlefs.h"
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

	esp_vfs_littlefs_conf_t conf = {
		.base_path = "/mid",
		.partition_label = "mid",
		.format_if_mount_failed = true,
		.dont_mount = false,
	};

	esp_err_t ret = esp_vfs_littlefs_register(&conf);
	TEST_ESP_OK(ret);

	// NB: ESP-IDF first printf does some allocations, so to not cause false positives in tests
	// just leave this here.
	printf("%s", "");

	// All tests that may leak (due to mbedtls)
	unity_utils_set_leak_level(2048);
	UNITY_BEGIN();
	unity_run_tests_by_tag("[allowleak]", false);
	UNITY_END();

	unity_utils_set_leak_level(0);
	UNITY_BEGIN();
	unity_run_tests_by_tag("[allowleak]", true);
	UNITY_END();

	unity_run_menu();

	TEST_ESP_OK(esp_vfs_fat_spiflash_unmount_rw_wl(TEST_PARTITION, s_wl_handle));
}
