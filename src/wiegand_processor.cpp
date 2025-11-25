#include "wiegand_processor.h"
#include "i2c_driver.h"
#include "card_formatter.h"
#include "config.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdio.h>

// Global variables
uint64_t wiegand_data = 0; 
uint8_t wiegand_bit_count = 0;
uint32_t wiegand_last_bit_time = 0;
bool wiegand_data_ready = false;
uint32_t total_bits_received = 0;
uint32_t card_read_count = 0;

// Добавляем флаг для подавления лишнего вывода
static bool debug_output = true;

void check_wiegand() {
    uint8_t data;
    esp_err_t ret = pcf8574_read(CONFIG_I2C_INPUTS1_ADDRESS, &data);
    
    if (ret != ESP_OK) {
        if (debug_output) {
            printf("❌ I2C read error: %d\n", ret);
        }
        return;
    }
    
    uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    
    bool d0_state = (data & (1 << (WIEGAND_D0 - 1))) == 0;
    bool d1_state = (data & (1 << (WIEGAND_D1 - 1))) == 0;
    
    static uint8_t last_d0 = 1;
    static uint8_t last_d1 = 1;
    
    // Обнаружение фронтов
    if (d0_state && !last_d0) {
        handle_wiegand_bit(0);
    }
    
    if (d1_state && !last_d1) {
        handle_wiegand_bit(1);
    }
    
    last_d0 = d0_state;
    last_d1 = d1_state;
    
    // Таймаут только если есть данные и прошло достаточно времени
    if (wiegand_bit_count > 0 && (current_time - wiegand_last_bit_time) > WIEGAND_TIMEOUT_MS) {
        wiegand_data_ready = true;
        if (debug_output) {
            printf("⏰ Timeout triggered, bits: %d\n", wiegand_bit_count);
        }
    }
}

void handle_wiegand_bit(uint8_t bit) {
    uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    
    // Сброс если прошло много времени с последнего бита
    if ((current_time - wiegand_last_bit_time) > WIEGAND_TIMEOUT_MS) {
        wiegand_data = 0;
        wiegand_bit_count = 0;
        if (debug_output) {
            printf("🔄 Reset Wiegand data\n");
        }
    }
    
    wiegand_data = (wiegand_data << 1ULL) | (uint64_t)bit;
    wiegand_bit_count++;
    total_bits_received++;
    wiegand_last_bit_time = current_time;
    
    if (debug_output && wiegand_bit_count <= 3) {
        printf("📊 Bit: %d, Total bits: %d\n", bit, wiegand_bit_count);
    }
}

void process_wiegand_data() {
    if (wiegand_bit_count == 0) return;
    
    card_read_count++;
    
    printf("\n🎫 === WIEGAND CARD DETECTED ===\n");
    printf("🔢 Raw Bit count: %d\n", wiegand_bit_count);
    printf("🔢 Raw Data: 0x%016llX\n", wiegand_data);
    
    printf("🔍 Analysis:\n");
    if (wiegand_bit_count < 26) {
        printf("❌ TOO FEW BITS! Expected 26-58, got %d\n", wiegand_bit_count);
    } else if (wiegand_bit_count == 26) {
        process_26bit_wiegand();
    } else if (wiegand_bit_count == 34) {
        process_34bit_wiegand();
    } else if (wiegand_bit_count == 37) {
        process_37bit_wiegand();
    } else if (wiegand_bit_count >= 56 && wiegand_bit_count <= 58) {
        process_56bit_wiegand();
    } else {
        printf("❓ Unknown Wiegand format: %d bits\n", wiegand_bit_count);
    }
    
    printf("📈 Stats: Cards: %lu, Total bits: %lu\n", card_read_count, total_bits_received);
    printf("=============================\n\n");

    reset_wiegand();
}

// Остальные функции без изменений...
void process_26bit_wiegand() {
    uint8_t facility_code = (uint8_t)((wiegand_data >> 17ULL) & 0xFFULL);
    uint16_t card_code = (uint16_t)((wiegand_data >> 1ULL) & 0xFFFFULL);
    
    printf("✅ 26-bit Format:\n");
    printf("🏢 Facility Code: %d\n", facility_code);
    printf("💳 Card Number: %d\n", card_code);
}

void process_34bit_wiegand() {
    printf("✅ 34-bit Format:\n");
    uint64_t clean_data = (wiegand_data >> 1) & 0xFFFFFFFFULL;
    printf("💳 Card ID: %lu\n", (uint32_t)clean_data);
}

void process_37bit_wiegand() {
    printf("✅ 37-bit Format:\n");
    uint64_t clean_data = (wiegand_data >> 1) & 0x7FFFFFFFFULL; 
    printf("💳 Card ID: %lu\n", (uint32_t)clean_data);
}

void process_56bit_wiegand() {
    uint64_t clean_data = wiegand_data;
    
    if (wiegand_bit_count == 58) {
        printf("⚠️  Detected 58 bits (Data + Parity). Stripping parity bits...\n");
        clean_data = (wiegand_data >> 1) & 0x00FFFFFFFFFFFFFFULL;
    } 
    else if (wiegand_bit_count == 56) {
         clean_data = wiegand_data;
    }
    else {
        printf("⚠️  Warning: Odd bit count (%d). Result may be inaccurate.\n", wiegand_bit_count);
    }

    printf("✅ 56-bit Format (Processed):\n");
    
    uint8_t facility_code = (uint8_t)((clean_data >> 48ULL) & 0xFFULL);
    uint16_t site_code = (uint16_t)((clean_data >> 32ULL) & 0xFFFFULL);
    uint32_t card_number = (uint32_t)(clean_data & 0xFFFFFFFFULL);
    
    printf("🏢 Facility Code: %d\n", facility_code);
    printf("🏠 Site Code: %d\n", site_code);
    printf("💳 Card Number: %lu\n", card_number);
    
    // Вызов функции форматирования
    format_serial_hex_7bytes(clean_data, 56);
}

void reset_wiegand() {
    wiegand_data = 0;
    wiegand_bit_count = 0;
    wiegand_last_bit_time = 0;
    wiegand_data_ready = false;
}

void speed_test() {
    static uint32_t last_test = 0;
    uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    
    if (current_time - last_test > 10000) {
        last_test = current_time;
        // Убираем вывод чтобы не засорять консоль
    }
}

// Функция для управления отладочным выводом
void set_wiegand_debug(bool enable) {
    debug_output = enable;
}