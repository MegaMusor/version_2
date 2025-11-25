#ifndef I2C_DRIVER_H
#define I2C_DRIVER_H

#include <driver/i2c.h>
#include <esp_err.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t i2c_master_init(void);
esp_err_t pcf8574_read(uint8_t addr, uint8_t *data);

#ifdef __cplusplus
}
#endif

#endif // I2C_DRIVER_H