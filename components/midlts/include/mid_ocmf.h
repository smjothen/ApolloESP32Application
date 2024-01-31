#ifndef __MID_OCMF_H__
#define __MID_OCMF_H__

#include "cJSON.h"
#include "utz.h"

#include "mid_event.h"
#include "mid_sign.h"
#include "mid_lts.h"
#include "mid_ocmf.h"
#include "mid_session.h"

const char *midocmf_signed_transaction_from_active_session(mid_sign_ctx_t *ctx, const char *serial, midlts_active_t *active_session);

const char *midocmf_signed_fiscal_from_meter_value(mid_sign_ctx_t *ctx, const char *serial, mid_session_meter_value_t *value, mid_event_log_t *log);
const char *midocmf_signed_fiscal_from_record(mid_sign_ctx_t *ctx, const char *serial, mid_session_record_t *value, mid_event_log_t *log);

#endif
