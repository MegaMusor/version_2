#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "i2c_driver.h"
#include "wiegand_processor.h"
#include "search.h"
#include "config.h"

// Глобальные переменные для статистики
static uint32_t last_card_time = 0;
static uint32_t cards_per_minute = 0;
static uint32_t card_count = 0;

// Задача для опроса датчика (ядро 1)
void sensor_task(void *pvParameter) {
    printf("📡 Sensor task started on core %d\n", xPortGetCoreID());
    
    // Включаем отладку, чтобы видеть сырые данные
    set_wiegand_debug(true);
    
    while (1) {
        check_wiegand();
        
        if (wiegand_data_ready) {
            
            // ! КРИТИЧЕСКИЙ ШАГ: СРАЗУ ЗАХВАТЫВАЕМ ГЛОБАЛЬНЫЕ ДАННЫЕ
            // Это решает проблему чтения глобальных переменных после их возможного сброса.
            uint64_t captured_data = wiegand_data;
            uint8_t captured_bits = wiegand_bit_count;

            // 1. Обрабатываем данные Wiegand (для вывода в консоль и сброса флага ready)
            // Эта функция использует ГЛОБАЛЬНЫЕ переменные для печати и сбрасывает флаг.
            process_wiegand_data(); 
            
            // 2. Проверка на мусорные данные (0x0)
            if (captured_data == 0 || captured_bits == 0) {
                printf("❌ Ошибка: Получен пустой/недействительный пакет (0x0, %d бит). Игнорирую.\n", captured_bits);
                continue; // Начинаем новый цикл
            }
            
            uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
            
            // 3. Обновляем статистику
            card_count++;
            if (current_time - last_card_time < 60000) {
                cards_per_minute = (card_count * 60000) / ((current_time - last_card_time) + 1);
            } else {
                cards_per_minute = card_count;
                card_count = 0;
                last_card_time = current_time;
            }
            
            // 4. Подготовка данных для поиска (используем захваченные ЛОКАЛЬНЫЕ данные)
            uint64_t search_data = captured_data;

            // Если это 58 бит (с четностью), обрезаем лишнее
            if (captured_bits == 58) {
                search_data = (captured_data >> 1) & 0x00FFFFFFFFFFFFFFULL;
            }
            // Для остальных форматов отправляем как есть
            
            printf("🚀 Отправка в поиск HEX: 0x%014llX (Бит: %d)\n", search_data, captured_bits);
            
            // 5. Отправляем ЛОКАЛЬНУЮ переменную в очередь (FreeRTOS Queue)
            add_card_to_search_queue(search_data);
        }
        
        speed_test();
        vTaskDelay(5 / portTICK_PERIOD_MS);
    }
}

// Задача для вывода статистики
void stats_task(void *pvParameter) {
    while (1) {
        // Проверяем свободное место в стеке
        UBaseType_t stack_high_water_mark = uxTaskGetStackHighWaterMark(NULL);
        
        if (search_queue != NULL) {
            printf("📊 Статистика: %lu карт/мин | Очередь: %d | Free Stack: %d\n", 
                   cards_per_minute, 
                   uxQueueMessagesWaiting(search_queue),
                   stack_high_water_mark);
        } else {
            printf("📊 Статистика: %lu карт/мин | Очередь: --\n", cards_per_minute);
        }
        vTaskDelay(10000 / portTICK_PERIOD_MS);
    }
}

extern "C" void app_main() {
    printf("=== WIEGAND READER - FIXED VERSION ===\n");
    printf("📍 D0: Input %d, D1: Input %d\n", WIEGAND_D0, WIEGAND_D1);
    
    // Инициализация I2C
    esp_err_t ret = i2c_master_init();
    if (ret != ESP_OK) {
        printf("❌ I2C init FAILED: %d\n", ret);
        return;
    }
    printf("✅ I2C initialized\n");

    // Инициализация файловой системы
    init_spiffs();
    
    // Инициализация базы данных и ТЕСТОВЫХ КАРТ
    generate_data_if_needed();
    load_indices();
    print_storage_info();
    
    // Показываем какие карты сейчас в памяти как тестовые
    print_test_cards_info();
    
    // Запускаем задачу поиска (Ядро 0)
    start_search_task();
    
    // Запускаем задачу датчика (Ядро 1)
    xTaskCreatePinnedToCore(
        sensor_task,
        "sensor_task",
        4096,
        NULL,
        1,
        NULL,
        1
    );
    
    // Запускаем задачу статистики с УВЕЛИЧЕННЫМ стеком
    xTaskCreate(
        stats_task,
        "stats_task",
        4096,   
        NULL,
        1,
        NULL
    );
    
    printf("✅ Система запущена. Приложите карту...\n");

    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}