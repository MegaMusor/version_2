#include "search.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_random.h"
#include "esp_timer.h" 
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h" // Важно добавить этот инклюд

// ==========================================
// НАСТРОЙКИ И ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ
// ==========================================
#define TOTAL_FILES 10
#define RECORDS_PER_FILE 1000
#define RECORD_BITS 86
#define FILE_SIZE_BYTES ((RECORDS_PER_FILE * RECORD_BITS) / 8)
#define MOUNT_POINT "/spiffs"
#define SEARCH_QUEUE_SIZE 10

uint64_t file_start_ids[TOTAL_FILES];
QueueHandle_t search_queue = NULL;
static bool spiffs_initialized = false;

// ==========================================
// ТЕСТОВЫЕ КАРТЫ (добавьте свои HEX-идентификаторы)
// ==========================================

struct TestCard {
    uint64_t hex_id;
    const char* name;
    const char* description;
};

// ЗАМЕНИТЕ ЭТИ ПРИМЕРЫ НА РЕАЛЬНЫЕ HEX-ИДЕНТИФИКАТОРЫ ВАШИХ КАРТ
// Обновляем массив тестовых карт в search.cpp
static TestCard test_cards[] = {
    // Я использую ваше значение 0x9011953AFF5404 для тестовой карты 5, 
    // чтобы она гарантированно находилась в быстром поиске.
    {0x9011953AA81F04ULL, "Карта 1", "жески элеватор флекс"},
    {0x9011953AD66404ULL, "Карта 2", "жучий свег"},
};

#define TEST_CARDS_COUNT (sizeof(test_cards) / sizeof(test_cards[0]))

// ==========================================
// FORWARD DECLARATIONS
// ==========================================

uint64_t extract_bits_from_ram(const uint8_t* buffer, uint64_t global_bit_start, int bit_count);
void get_card_from_buffer(const uint8_t* buffer, int index, CardInfo* out);
void push_bits(uint8_t* buffer, int* bit_cursor, uint64_t value, int width);
void print_card_info(const CardInfo* ci, int file_idx, int rec_idx);

void generate_data_if_needed();
void load_indices();
void add_test_cards_to_database();
void init_spiffs();
void print_storage_info();
void print_test_cards_info();

// ==========================================
// ФУНКЦИИ ДЛЯ ТЕСТОВЫХ КАРТ
// ==========================================

void print_test_cards_info() {
    printf("\n🎯 === ТЕСТОВЫЕ КАРТЫ ДЛЯ ПРОВЕРКИ ===\n");
    for (int i = 0; i < TEST_CARDS_COUNT; i++) {
        printf("🔑 Card %d: 0x%014llX - %s\n", 
               i + 1, test_cards[i].hex_id, test_cards[i].name);
        printf("   📝 %s\n", test_cards[i].description);
    }
    printf("💡 Используйте эти HEX-коды для тестирования поиска\n");
    printf("==========================================\n\n");
}

void add_test_cards_to_database() {
    if (!spiffs_initialized) {
        printf("❌ SPIFFS не инициализирован - нельзя добавить тестовые карты\n");
        return;
    }

    printf("\n🔧 Добавление тестовых карт в базу данных...\n");
    
    // Для простоты добавим тестовые карты в первый файл
    char fname[32];
    snprintf(fname, sizeof(fname), "%s/data_0.bin", MOUNT_POINT);
    
    FILE* fd = fopen(fname, "r+b"); // Открываем для чтения и записи
    if (!fd) {
        printf("❌ Не могу открыть файл для добавления тестовых карт\n");
        return;
    }

    // Читаем весь файл в память
    uint8_t* file_buffer = (uint8_t*)malloc(FILE_SIZE_BYTES);
    if (!file_buffer) {
        printf("❌ Не могу выделить память\n");
        fclose(fd);
        return;
    }
    
    fread(file_buffer, 1, FILE_SIZE_BYTES, fd);
    
    int cards_added = 0;
    
    // Заменяем первые несколько записей на тестовые карты
    for (int i = 0; i < TEST_CARDS_COUNT && i < 10; i++) {
        int record_index = i; // Заменяем первые записи
        
        uint64_t start_bit = (uint64_t)record_index * RECORD_BITS;
        
        // Очищаем старые данные
        for (int bit = 0; bit < RECORD_BITS; bit++) {
            uint64_t current_bit_pos = start_bit + bit;
            uint32_t byte_idx = current_bit_pos / 8;
            uint8_t  bit_idx  = current_bit_pos % 8;
            file_buffer[byte_idx] &= ~(1 << (7 - bit_idx));
        }
        
        // Записываем новую карту
        int bit_cursor = start_bit;
        push_bits(file_buffer, &bit_cursor, test_cards[i].hex_id, 56);
        push_bits(file_buffer, &bit_cursor, 1, 2);  // status: активна
        push_bits(file_buffer, &bit_cursor, 0, 4);  // count: 0
        push_bits(file_buffer, &bit_cursor, 0xFF, 8); // zones: все зоны
        push_bits(file_buffer, &bit_cursor, 0, 16); // link: 0
        
        cards_added++;
        printf("✅ Добавлена тестовая карта: 0x%014llX - %s\n", 
               test_cards[i].hex_id, test_cards[i].name);
    }
    
    // Записываем изменения обратно в файл
    fseek(fd, 0, SEEK_SET);
    fwrite(file_buffer, 1, FILE_SIZE_BYTES, fd);
    fclose(fd);
    free(file_buffer);
    
    printf("🎉 Добавлено %d тестовых карт в базу данных\n\n", cards_added);
    
    // Перезагружаем индексы
    load_indices();
}

// ==========================================
// ОБНОВЛЕННАЯ ФУНКЦИЯ ПОИСКА
// ==========================================

// ...
void search_card(uint64_t target_hex) {
    if (!spiffs_initialized) {
        printf("❌ SPIFFS не инициализирован - поиск невозможен\n");
        return;
    }
    
    // Запускаем замер времени прямо в начале функции
    int64_t t_start = esp_timer_get_time(); // <--- ДОБАВЛЕНО/ПЕРЕМЕЩЕНО

    // 1. Быстрая проверка тестовых карт
    for (int i = 0; i < TEST_CARDS_COUNT; i++) {
        if (test_cards[i].hex_id == target_hex) {
            
            // Расчет времени в наносекундах
            int64_t search_time_us = esp_timer_get_time() - t_start;
            uint64_t search_time_ns = (uint64_t)search_time_us * 1000;
            
            printf("\n🎉 === ТЕСТОВАЯ КАРТА ОБНАРУЖЕНА! ===\n");
            printf("⏱️  Время поиска: %llu нс\n", search_time_ns); // <--- ДОБАВЛЕНО
            printf("🔑 HEX: 0x%014llX\n", target_hex);
            printf("🏷️  Название: %s\n", test_cards[i].name);
            printf("📝 Описание: %s\n", test_cards[i].description);
            printf("✅ Статус: Карта активна и разрешена\n");
            printf("🎯 Результат: ДОСТУП РАЗРЕШЕН\n");
            printf("================================\n\n");
            return;
        }
    }
    
    // 2. Подготовка к поиску в базе

    int file_idx = -1;
    for (int i = 0; i < TOTAL_FILES; i++) {
        bool is_candidate = (target_hex >= file_start_ids[i]);
        bool next_file_starts_later = (i == TOTAL_FILES - 1) || (target_hex < file_start_ids[i+1]);
        if (is_candidate && next_file_starts_later) {
            file_idx = i;
            break;
        }
    }

    if (file_idx == -1) {
        printf("🔍 Результат: HEX 0x%llX вне диапазона базы данных\n", target_hex);
        printf("❌ ДОСТУП ЗАПРЕЩЕН - карта не найдена в системе\n");
        return;
    }

    char fname[32];
    snprintf(fname, sizeof(fname), "%s/data_%d.bin", MOUNT_POINT, file_idx);
    FILE* fd = fopen(fname, "rb");
    if (!fd) {
        printf("❌ Ошибка открытия файла: %s\n", fname);
        return;
    }

    uint8_t* file_buffer = (uint8_t*)malloc(FILE_SIZE_BYTES);
    if (!file_buffer) {
        printf("❌ Ошибка выделения памяти\n");
        fclose(fd);
        return;
    }
    
    fread(file_buffer, 1, FILE_SIZE_BYTES, fd);
    fclose(fd);

    // 3. Бинарный поиск
    int left = 0, right = RECORDS_PER_FILE - 1;
    bool found = false;
    CardInfo ci;

    while (left <= right) {
        int mid = left + (right - left) / 2;
        uint64_t mid_id = extract_bits_from_ram(file_buffer, (uint64_t)mid * RECORD_BITS, 56);

        if (mid_id == target_hex) {
            get_card_from_buffer(file_buffer, mid, &ci);
            found = true;
            
            // --- ИСПРАВЛЕНИЕ: ПЕРЕВОД В НС ---
            int64_t search_time_us = esp_timer_get_time() - t_start;
            uint64_t search_time_ns = (uint64_t)search_time_us * 1000;
            
            printf("\n🎉 === КАРТА НАЙДЕНА В БАЗЕ ДАННЫХ ===\n");
            printf("⏱️  Время поиска: %llu нс\n", search_time_ns); // Вывод в наносекундах
            printf("🔑 HEX: 0x%014llX\n", ci.hex_id);
            printf("📊 Статус: %s\n", ci.status == 1 ? "АКТИВНА" : "ЗАБЛОКИРОВАНА");
            printf("🔢 Счетчик использований: %d\n", ci.count);
            printf("🚪 Доступные зоны: 0x%02X\n", ci.zones);
            printf("🔗 Ссылка: %d\n", ci.link);
            printf("📁 Местоположение: Файл %d, Запись %d\n", file_idx, mid);
            
            if (ci.status == 1) {
                printf("✅ ДОСТУП РАЗРЕШЕН\n");
            } else {
                printf("❌ ДОСТУП ЗАПРЕЩЕН - карта заблокирована\n");
            }
            printf("================================\n\n");
            break;
        }
        if (mid_id < target_hex) left = mid + 1;
        else right = mid - 1;
    }
    free(file_buffer);
    
    if (!found) {
        printf("🔍 Результат: Карта 0x%llX не найдена в базе данных\n", target_hex);
        printf("❌ ДОСТУП ЗАПРЕЩЕН\n");
    }
}

// ==========================================
// ОБНОВЛЕННАЯ ФУНКЦИЯ ГЕНЕРАЦИИ ДАННЫХ
// ==========================================

void generate_data_if_needed() {
    if (!spiffs_initialized) {
        printf("❌ SPIFFS не инициализирован - нельзя генерировать данные\n");
        return;
    }
    
    struct stat st;
    if (stat(MOUNT_POINT "/data_0.bin", &st) == 0) {
        printf("✅ База данных уже существует\n");
        
        // Добавляем тестовые карты в существующую базу
        add_test_cards_to_database();
        return;
    }

    printf("📁 Генерация базы данных карт...\n");
    uint8_t* ram_buf = (uint8_t*)malloc(FILE_SIZE_BYTES);
    if (!ram_buf) {
        printf("❌ Ошибка выделения памяти для базы данных\n");
        return;
    }
    
    uint64_t current_hex = 0x10000000000000;

    for (int f = 0; f < TOTAL_FILES; f++) {
        memset(ram_buf, 0, FILE_SIZE_BYTES);
        int bit_cursor = 0;
        for (int r = 0; r < RECORDS_PER_FILE; r++) {
            current_hex += (esp_random() % 50) + 1; 
            push_bits(ram_buf, &bit_cursor, current_hex, 56);
            push_bits(ram_buf, &bit_cursor, esp_random() % 4, 2);
            push_bits(ram_buf, &bit_cursor, esp_random() % 16, 4);
            push_bits(ram_buf, &bit_cursor, esp_random() % 255, 8);
            push_bits(ram_buf, &bit_cursor, esp_random() % 60000, 16);
        }
        char fname[32];
        snprintf(fname, sizeof(fname), "%s/data_%d.bin", MOUNT_POINT, f);
        FILE* fd = fopen(fname, "wb");
        if (fd) {
            fwrite(ram_buf, 1, FILE_SIZE_BYTES, fd);
            fclose(fd);
            printf("📄 Создан файл: %s\n", fname);
        } else {
            printf("❌ Ошибка создания файла: %s\n", fname);
        }
    }
    free(ram_buf);
    printf("✅ База данных сгенерирована\n");
    
    // Добавляем тестовые карты после генерации
    add_test_cards_to_database();
}

// ==========================================
// ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ (BITS)
// ==========================================

uint64_t extract_bits_from_ram(const uint8_t* buffer, uint64_t global_bit_start, int bit_count) {
    uint64_t result = 0;
    for (int i = 0; i < bit_count; i++) {
        uint64_t current_bit_pos = global_bit_start + i;
        uint32_t byte_idx = current_bit_pos / 8;
        uint8_t  bit_idx  = current_bit_pos % 8; 
        uint8_t bit = (buffer[byte_idx] >> (7 - bit_idx)) & 1;
        result = (result << 1) | bit;
    }
    return result;
}

void get_card_from_buffer(const uint8_t* buffer, int index, CardInfo* out) {
    uint64_t start_bit = (uint64_t)index * RECORD_BITS;
    out->hex_id = extract_bits_from_ram(buffer, start_bit, 56);
    start_bit += 56;
    out->status = (uint8_t)extract_bits_from_ram(buffer, start_bit, 2);
    start_bit += 2;
    out->count = (uint8_t)extract_bits_from_ram(buffer, start_bit, 4);
    start_bit += 4;
    out->zones = (uint8_t)extract_bits_from_ram(buffer, start_bit, 8);
    start_bit += 8;
    out->link = (uint16_t)extract_bits_from_ram(buffer, start_bit, 16);
}

void push_bits(uint8_t* buffer, int* bit_cursor, uint64_t value, int width) {
    for (int i = width - 1; i >= 0; i--) {
        uint8_t bit = (value >> i) & 1;
        int byte_idx = (*bit_cursor) / 8;
        int bit_idx = (*bit_cursor) % 8;
        if (bit) buffer[byte_idx] |= (1 << (7 - bit_idx));
        else     buffer[byte_idx] &= ~(1 << (7 - bit_idx));
        (*bit_cursor)++;
    }
}

void print_card_info(const CardInfo* ci, int file_idx, int rec_idx) {
    printf("  [F:%d R:%03d] HEX: 0x%014llX | St:%d | Cnt:%2d | Zn:0x%02X | Lnk:%5d\n", 
            file_idx, rec_idx, ci->hex_id, ci->status, ci->count, ci->zones, ci->link);
}

// ==========================================
// ФУНКЦИИ ИНИЦИАЛИЗАЦИИ И СИСТЕМНЫЕ
// ==========================================

void init_spiffs() {
    printf("🔧 Инициализация SPIFFS...\n");
    esp_vfs_spiffs_conf_t conf = {
        .base_path = MOUNT_POINT,
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true
    };
    
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        printf("❌ Ошибка SPIFFS: %s\n", esp_err_to_name(ret));
        return;
    }
    spiffs_initialized = true;
    printf("✅ SPIFFS готов\n");
}

void load_indices() {
    if (!spiffs_initialized) return;
    printf("📑 Загрузка индексов...\n");
    
    uint8_t* temp_buf = (uint8_t*)malloc(16);
    for (int i = 0; i < TOTAL_FILES; i++) {
        char fname[32];
        snprintf(fname, sizeof(fname), "%s/data_%d.bin", MOUNT_POINT, i);
        FILE* fd = fopen(fname, "rb");
        if (fd) {
            fread(temp_buf, 1, 8, fd);
            file_start_ids[i] = extract_bits_from_ram(temp_buf, 0, 56);
            fclose(fd);
        } else {
            file_start_ids[i] = 0xFFFFFFFFFFFFFFFFULL;
        }
    }
    free(temp_buf);
    printf("✅ Индексы загружены\n");
}

void print_storage_info() {
    if (!spiffs_initialized) return;
    size_t total = 0, used = 0;
    esp_spiffs_info(NULL, &total, &used);
    printf("💾 Storage: %d / %d KB used\n", used/1024, total/1024);
}

// ==========================================
// ЗАДАЧИ FREERTOS
// ==========================================

void search_worker_task(void *pvParameters) {
    uint64_t card_to_search;
    while (1) {
        // Ждем карту в очереди
        if (xQueueReceive(search_queue, &card_to_search, portMAX_DELAY) == pdTRUE) {
            search_card(card_to_search);
        }
    }
}

void start_search_task() {
    search_queue = xQueueCreate(SEARCH_QUEUE_SIZE, sizeof(uint64_t));
    if (search_queue == NULL) {
        printf("❌ Ошибка создания очереди\n");
        return;
    }

    xTaskCreatePinnedToCore(search_worker_task, "search_worker", 8192, NULL, 1, NULL, 0);
    printf("✅ Задача поиска запущена\n");
}

void add_card_to_search_queue(uint64_t card_hex) {
    if (search_queue != NULL) {
        xQueueSend(search_queue, &card_hex, 0);
    } else {
        printf("⚠️ Очередь не готова\n");
    }
}