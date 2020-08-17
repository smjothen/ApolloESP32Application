
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void I2CDevicesInit();
void do_i2cdetect_cmd();
esp_err_t i2c_master_write_slave(uint8_t slave_addr, uint8_t *data_wr, size_t size);
esp_err_t i2c_master_read_slave(uint8_t slave_addr, uint8_t *data_rd, size_t size);

esp_err_t i2c_master_write_slave_at_address(uint8_t slave_addr, uint16_t wr_reg, uint8_t *data_wr, size_t size);
esp_err_t i2c_master_read_slave_at_address(uint8_t slave_addr, uint16_t rd_reg, uint8_t *data_rd, size_t size);

#ifdef __cplusplus
}
#endif
