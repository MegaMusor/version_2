#include "i2c_driver.h"
#include "config.h"

esp_err_t i2c_master_init() {
    i2c_config_t conf;
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = I2C_MASTER_SDA_IO;
    conf.scl_io_num = I2C_MASTER_SCL_IO;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = I2C_MASTER_FREQ_HZ;
    
    esp_err_t ret = i2c_param_config(I2C_MASTER_NUM, &conf);
    if (ret != ESP_OK) return ret;
    
    return i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
}

esp_err_t pcf8574_read(uint8_t addr, uint8_t *data) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(cmd, data, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    
    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, 50 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    return ret;
}