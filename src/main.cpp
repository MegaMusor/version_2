#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "i2c_driver.h"
#include "wiegand_processor.h"
#include "config.h"

extern "C" void app_main() {
    printf("=== WIEGAND READER - HYBRID HEX FORMAT ===\n");
    printf("📍 D0: Input %d, D1: Input %d\n", WIEGAND_D0, WIEGAND_D1);
    printf("🏷️  PCF8574 Address: 0x%02X\n", CONFIG_I2C_INPUTS1_ADDRESS);
    printf("🔧 Waiting for cards...\n\n");
    
    esp_err_t ret = i2c_master_init();
    if (ret != ESP_OK) {
        printf("❌ I2C init FAILED: %d\n", ret);
        return;
    }
    printf("✅ I2C initialized successfully\n");

    uint8_t test_data;
    if (pcf8574_read(CONFIG_I2C_INPUTS1_ADDRESS, &test_data) == ESP_OK) {
        printf("📊 PCF8574 Connected. State: 0x%02X\n", test_data);
    } else {
        printf("❌ Failed to read PCF8574\n");
    }
    
    while (1) {
        check_wiegand();
        
        if (wiegand_data_ready) {
            process_wiegand_data();
        }
        
        speed_test(); 
        
        vTaskDelay(1 / portTICK_PERIOD_MS); 
    }
}