#ifndef CONFIG_H
#define CONFIG_H

// I2C Configuration
#define I2C_MASTER_SCL_IO 10
#define I2C_MASTER_SDA_IO 9
#define I2C_MASTER_FREQ_HZ 100000
#define I2C_MASTER_NUM I2C_NUM_0

// PCF8574 Address
#define CONFIG_I2C_INPUTS1_ADDRESS 0x22

// Wiegand Configuration
#define WIEGAND_D0 5
#define WIEGAND_D1 6
#define WIEGAND_TIMEOUT_MS 25

#endif // CONFIG_H