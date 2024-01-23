#ifndef __MID_OCMF_H__
#define __MID_OCMF_H__

#include "cJSON.h"
#include "utz.h"
#include "mid_session.h"

int midocmf_create_fiscal_message(char *buf, size_t size, const char *serial, mid_session_record_t *rec);

#endif
