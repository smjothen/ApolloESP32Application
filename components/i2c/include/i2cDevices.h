/* cmd_i2ctools.h

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

//void register_i2ctools(void);
void I2CDevicesInit();
//esp_err_t i2c_master_write_slave(uint8_t slave_addr, uint8_t *data_wr, size_t size);
//esp_err_t i2c_master_read_slave(uint8_t slave_addr, uint8_t *data_rd, size_t size);

#ifdef __cplusplus
}
#endif
