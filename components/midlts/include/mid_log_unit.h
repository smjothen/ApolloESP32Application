#ifndef __MIDLOG_UNIT_H__
#define __MIDLOG_UNIT_H__

// Allow explicit setting of energy and time for unit tests
//
#ifdef UNIT_TEST
#warning "Enabling MID log unit test mode!"

int midlog_append_energy_private(midlog_ctx_t *ctx, time_t timestamp, uint32_t energy);

#endif

#endif /* __MIDLOG_UNIT_H__ */
