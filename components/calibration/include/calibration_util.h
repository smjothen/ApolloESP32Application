#ifndef __CALUTIL_H__
#define __CALUTIL_H__

#include "calibration.h"

const char *calibration_state_to_string(CalibrationState state);
const char *calibration_step_to_string(CalibrationStep state);
const char *charger_state_to_string(ChargerState state);

bool calibration_ref_voltage_is_recent(CalibrationCtx *ctx);
bool calibration_ref_current_is_recent(CalibrationCtx *ctx);
bool calibration_ref_energy_is_recent(CalibrationCtx *ctx);

double calibration_scale_emeter(CalibrationState state, double raw);
double calibration_inv_scale_emeter(CalibrationState state, float raw);

bool calibration_get_emeter_snapshot(CalibrationCtx *ctx, uint8_t *source, float *ivals, float *vvals);
uint16_t calibration_read_samples(void);

bool calibration_read_average(CalibrationCtx *ctx, int phase, float *average) ;
uint16_t calibration_get_emeter_averages(CalibrationCtx *ctx, int wait_for_samples, float *averages);

bool calibration_open_relays(CalibrationCtx *ctx);
bool calibration_close_relays(CalibrationCtx *ctx);

bool calibration_start_calibration_run(CalibrationCtx *ctx, CalibrationType type);

#endif
