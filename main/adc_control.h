#ifndef _ADC_CONTROL_H_
#define _ADC_CONTROL_H_

#ifdef __cplusplus
extern "C" {
#endif

uint8_t HANEnergyLevel;
float hwIdVoltageLevel;
uint8_t GetHANEnergyLevel();
float GetHwIdVoltageLevel();
void adc_init();



#ifdef __cplusplus
}
#endif

#endif  /*_DRIVER_ADC_H_*/
