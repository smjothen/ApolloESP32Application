#ifndef __MID_ACTIVE_H__
#define __MID_ACTIVE_H__

#include "mid_lts.h"

midlts_err_t midlts_active_session_alloc(midlts_active_t *active);
midlts_err_t midlts_active_session_append(midlts_active_t *active, mid_session_meter_value_t *rec);
void midlts_active_session_set_id(midlts_active_t *active, mid_session_id_t *id);
void midlts_active_session_set_auth(midlts_active_t *active, mid_session_auth_t *auth);
void midlts_active_session_reset(midlts_active_t *active);
void midlts_active_session_free(midlts_active_t *active);

#endif
