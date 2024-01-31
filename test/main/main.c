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

#include "mid_lts.h"
#include "mid_sign.h"

#define TEST_PARTITION "/files"

const char *TAG = "UNITTEST";

static char ocmf_pub[512] = "-----BEGIN PUBLIC KEY-----\n"
"MHYwEAYHKoZIzj0CAQYFK4EEACIDYgAEj6hkVvHVvhM8mFm1/CkkDPTMTf0nMikK\n"
"Pw57yHHVO5fJLTTfZdN78XXanzdAe6JK3KqIQj/QXV5HV1XOdZ1Dy0AXykw/h8VZ\n"
"f+B8Mw3BhRcKx27PBTBfe8y7HITM3MzS\n"
"-----END PUBLIC KEY-----\n";

static char ocmf_prv[512] = "-----BEGIN EC PRIVATE KEY-----\n"
"MIGkAgEBBDDn4fPF8Q992eRJY39nh7Gi8n7dq3hLnv8pQQaUNdI0OsPGiJFC8IEG\n"
"WzcTMpqyPemgBwYFK4EEACKhZANiAASPqGRW8dW+EzyYWbX8KSQM9MxN/ScyKQo/\n"
"DnvIcdU7l8ktNN9l03vxddqfN0B7okrcqohCP9BdXkdXVc51nUPLQBfKTD+HxVl/\n"
"4HwzDcGFFwrHbs8FMF97zLschMzczNI=\n"
"-----END EC PRIVATE KEY-----\n";

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
	mid_sign_ctx_t *ctx = mid_sign_ctx_get_global();
	TEST_ASSERT_EQUAL_INT(0, mid_sign_ctx_init(ctx, ocmf_prv, sizeof (ocmf_prv), ocmf_pub, sizeof (ocmf_pub)));

	printf("%s", ocmf_pub);

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

	unity_utils_set_leak_level(2048);
	unity_run_menu();

	// All tests that may leak (due to mbedtls)
	/*
	unity_utils_set_leak_level(2048);
	UNITY_BEGIN();
	unity_run_tests_by_tag("[allowleak]", false);
	UNITY_END();

	unity_utils_set_leak_level(0);
	UNITY_BEGIN();
	unity_run_tests_by_tag("[allowleak]", true);
	UNITY_END();
	*/

	TEST_ESP_OK(esp_vfs_fat_spiflash_unmount_rw_wl(TEST_PARTITION, s_wl_handle));
}
