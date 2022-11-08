#ifndef SFH7776_H
#define SFH7776_H

/**
 * SFH7776 is a proximity and ambient ligth sensor. The proximity range depend on configuration and relectivity of detected object.
 * Under ideal settings it has a maximum range of ca 250mm.
 */

esp_err_t SFH7776_set_register(uint8_t reg, uint8_t value);
esp_err_t SFH7776_get_register(uint8_t reg, uint8_t * value);
esp_err_t SFH7776_set_lsb_then_msb(uint8_t lsb_addr, uint16_t value);
esp_err_t SFH7776_get_lsb_then_msb(uint8_t lsb_addr, uint16_t * value);

/**
 * Detect the availability of the SFH7776 sensor.
 */
esp_err_t SFH7776_detect();

/**
 * Set the SYSTEM_CONTROL register (0x40h)
 * It controls the software and interrupt function. The interrupt pin is not available on the speed card and won't have any effect.
 * bit 7 van be set to initiate a reset, bit 6 is related to interrupt and bit 5-0 are read only.
 */
#define SFH7776_set_system_control(ctrl) ({SFH7776_set_register(0x40, ctrl);})
#define SFH7776_get_system_control(ctrl) ({SFH7776_get_register(0x40, ctrl);})

/**
 * set the MODE_CONTROL register (0x41h)
 * Used to set proximity sensor mode and timing for measurements. Bit 4 is the proximity sensor mode, bit 3-0 alters the timing. See datasheet for
 * allowed values.
 */
#define SFH7776_set_mode_control(mode) ({SFH7776_set_register(0x41, mode);})
#define SFH7776_get_mode_control(mode) ({SFH7776_get_register(0x41, mode);})

/**
 * Set the ALS_PS_CONTROL register (0x42h)
 * Used to change the Ambient light sensor (ALS) and Proximity sensor (PS) output mode. Bit 6 is the PS output (0: proximity output, 1: infrared DC level output),
 * bit 5-2 is the ALS gain for visible and infrared light, bit 1-0 is the LED current which affects the range (11: highest range, 01:lowest range).
 */
#define SFH7776_set_sensor_control(ctrl) ({SFH7776_set_register(0x42, ctrl);})
#define SFH7776_get_sensor_control(ctrl) ({SFH7776_get_register(0x42, ctrl);})

/**
 * Set the PERRSISTENCE register (0x43h)
 * Used to change the how interrupt interprets the proximity sensor values.
 * Bit 3-0 can be set to require multiple consecutive values to be the same or with the same treshold to set the interrupt.
 */
#define SFH7776_set_persistence_control(ctrl) ({SFH7776_set_register(0x43, ctrl);})
#define SFH7776_get_persistence_control(ctrl) ({SFH7776_get_register(0x43, ctrl);})

/**
 * Enables or disables the interrupt pin and attaches a isr callback (IO22)
 */
esp_err_t SFH7776_configure_interrupt_pin(bool on, gpio_isr_t handle);

#define SFH7776_get_proximity(proximity_out) ({SFH7776_get_lsb_then_msb(0x44, proximity_out);})

#define SFH7776_get_ambient_light_visibile(ambient_light_out) ({SFH7776_get_lsb_then_msb(0x46, ambient_light_out);})
#define SFH7776_get_ambient_light_ir(ambient_light_out) ({SFH7776_get_lsb_then_msb(0x48, ambient_light_out);})

/**
 * Set the INTERRUPT_CONTROL register (0x4ah)
 * Used to change interrupt behavior.
 * Bit 7-6 read only value of interrupt state for PS (proximity sensor) and ALS (Ambient light sensor) respectivly
 * Bit 5-4 how interrupt threshold values are used for PS.
 * Bit 3 can be set to re-assert interrupt state on equvalent value to last read.
 * Bit 2 can be set disable latch (update after each measurement)
 * 1-0 can be set to enable the trigger for ALS and PS repectivly.
 */
#define SFH7776_set_interrupt_control(ctrl) ({SFH7776_set_register(0x4a, ctrl);})
#define SFH7776_get_interrupt_control(ctrl) ({SFH7776_get_register(0x4a, ctrl);})

#define SFH7776_set_proximity_interrupt_high_threshold(threshold) ({SFH7776_set_lsb_then_msb(0x4b, threshold);})
#define SFH7776_get_proximity_interrupt_high_threshold(threshold) ({SFH7776_get_lsb_then_msb(0x4b, threshold);})

#define SFH7776_set_proximity_interrupt_low_threshold(threshold) ({SFH7776_set_lsb_then_msb(0x4d, threshold);})
#define SFH7776_get_proximity_interrupt_low_threshold(threshold) ({SFH7776_get_lsb_then_msb(0x4d, threshold);})

#endif /* SFH7776_H */
