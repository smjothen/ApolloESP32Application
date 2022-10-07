#ifndef __CALUTIL_H__
#define __CALUTIL_H__

#include "calibration.h"

const char *calibration_state_to_string(CalibrationCtx *ctx);
const char *calibration_step_to_string(CalibrationCtx *ctx);
const char *charger_state_to_string(CalibrationCtx *ctx);

bool calibration_ref_voltage_is_recent(CalibrationCtx *ctx);
bool calibration_ref_current_is_recent(CalibrationCtx *ctx);
bool calibration_ref_energy_is_recent(CalibrationCtx *ctx);
bool calibration_get_ref_unit(CalibrationCtx *ctx, CalibrationUnit unit, int phase, float *value);

double calibration_scale_emeter(CalibrationUnit unit, double raw);
double calibration_inv_scale_emeter(CalibrationUnit unit, float raw);

bool calibration_get_emeter_snapshot(CalibrationCtx *ctx, uint8_t *source, float *ivals, float *vvals);
uint16_t calibration_read_samples(void);

bool calibration_start_calibration_run(CalibrationType type);
bool calibration_read_average(CalibrationType type, int phase, float *average) ;
uint16_t calibration_get_emeter_averages(CalibrationType type, float *averages);

bool calibration_open_relays(CalibrationCtx *ctx);
bool calibration_close_relays(CalibrationCtx *ctx);

bool calibration_write_parameter(CalibrationCtx *ctx, CalibrationType type, int phase, float value);

bool calibration_get_energy_counter(float *energy);

#endif
