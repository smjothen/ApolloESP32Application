#ifndef SFH7776_H
#define SFH7776_H

/**
 * SFH7776 is a proximity and ambient ligth sensor. The proximity range depend on configuration and relectivity of detected object.
 * Under ideal settings it has a maximum range of ca 250mm.
 */

esp_err_t SFH7776_set_register(uint8_t reg, uint8_t value);
esp_err_t SFH7776_get_register(uint8_t reg, uint8_t * value);
esp_err_t SFH7776_read_lsb_then_msb(uint8_t lsb_addr, uint16_t * value);

/**
 * Test the availability of the SFH7776 sensor. TODO: change to detect instead of test once testing is complete
 */
esp_err_t SFH7776_test();

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

#define SFH7776_read_proximity(proximity_out) ({SFH7776_read_lsb_then_msb(0x44, proximity_out);})

#define SFH7776_read_ambient_light_visibile(ambient_light_out) ({SFH7776_read_lsb_then_msb(0x46, ambient_light_out);})
#define SFH7776_read_ambient_light_ir(ambient_light_out) ({SFH7776_read_lsb_then_msb(0x48, ambient_light_out);})


#endif /* SFH7776_H */
