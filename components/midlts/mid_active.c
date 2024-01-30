#include "mid_lts.h"
#include "mid_active.h"

static midlts_err_t midlts_active_session_grow(midlts_active_t *active, size_t capacity) {
	//ESP_LOGI(TAG, "MID Active Session: Grow %zu", capacity);

	active->events = realloc(active->events, sizeof (mid_session_meter_value_t) * capacity);

	if (!active) {
		return LTS_ALLOC;
	}

	active->capacity = capacity;
	return LTS_OK;
}

midlts_err_t midlts_active_session_alloc(midlts_active_t *active) {
	return midlts_active_session_grow(active, 64);
}

midlts_err_t midlts_active_session_append(midlts_active_t *active, mid_session_meter_value_t *rec) {

	if (active->count >= active->capacity) {
		midlts_err_t err;
		if ((err = midlts_active_session_grow(active, active->capacity * 2)) != LTS_OK) {
			return err;
		}
	}

	//ESP_LOGI(TAG, "MID Active Session: Append %zu", active->count);
	active->events[active->count++] = *rec;

	active->has_versions = true;
	active->lr = rec->lr;
	active->fw = rec->fw;

	return LTS_OK;
}

void midlts_active_session_set_id(midlts_active_t *active, mid_session_id_t *id) {
	//ESP_LOGI(TAG, "MID Active Session: Set Id");
	active->has_id = true;
	active->id = *id;
}

void midlts_active_session_set_auth(midlts_active_t *active, mid_session_auth_t *auth) {
	//ESP_LOGI(TAG, "MID Active Session: Set Auth");
	active->has_auth = true;
	active->auth = *auth;
}

void midlts_active_session_reset(midlts_active_t *active) {
	//ESP_LOGI(TAG, "MID Active Session: Reset");
	active->has_id = false;
	memset(&active->id, 0, sizeof (active->id));

	active->has_auth = false;
	memset(&active->auth, 0, sizeof (active->auth));

	active->has_versions = false;
	memset(&active->lr, 0, sizeof (active->lr));
	memset(&active->fw, 0, sizeof (active->fw));

	memset(active->events, 0, sizeof (mid_session_meter_value_t) * active->capacity);
	active->count = 0;
}

void midlts_active_session_free(midlts_active_t *active) {
	if (active->events) {
		free(active->events);
		active->events = NULL;
	}
}
