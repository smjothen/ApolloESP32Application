#ifndef _ADC_CONTROL_H_
#define _ADC_CONTROL_H_

#ifdef __cplusplus
extern "C" {
#endif

float GetHardwareId();
float GetPowerMeas();
void adc_init();
int adcGetStackWatermark();



#ifdef __cplusplus
}
#endif

#endif  /*_DRIVER_ADC_H_*/
