#ifndef __MID_OCMF_H__
#define __MID_OCMF_H__

#include "cJSON.h"
#include "utz.h"

#include "mid_event.h"
#include "mid_sign.h"
#include "mid_ocmf.h"
#include "mid_session.h"

int midocmf_fiscal_from_meter_value(char *outbuf, size_t size, const char *serial, mid_session_meter_value_t *value, mid_event_log_t *log);
int midocmf_fiscal_from_record(char *outbuf, size_t size, const char *serial, mid_session_record_t *value, mid_event_log_t *log);

int midocmf_fiscal_from_meter_value_signed(char *outbuf, size_t size, const char *serial, mid_session_meter_value_t *value, mid_event_log_t *log, mid_sign_ctx_t *sign);
int midocmf_fiscal_from_record_signed(char *outbuf, size_t size, const char *serial, mid_session_record_t *value, mid_event_log_t *log, mid_sign_ctx_t *sign);

#endif
