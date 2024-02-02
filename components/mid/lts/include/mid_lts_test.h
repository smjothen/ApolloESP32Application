#ifndef __MID_LTS_TEST_H__
#define __MID_LTS_TEST_H__

#include "mid_lts.h"
#include "mid_lts_priv.h"

// Functions that should only be used for unit testing, etc.
//
midlts_err_t mid_session_reset(void);
midlts_err_t mid_session_reset_page(size_t addr);
midlts_err_t mid_session_init_internal(midlts_ctx_t *ctx, size_t max_pages, mid_session_version_fw_t fw_version, mid_session_version_lr_t lr_version);

#endif
