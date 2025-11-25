#ifndef SEARCH_H
#define SEARCH_H

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

// Структура для хранения информации о карте
struct CardInfo {
    uint64_t hex_id;
    uint8_t status;
    uint8_t count;
    uint8_t zones;
    uint16_t link;
};

// Объявляем очередь как extern чтобы была доступна из других файлов
extern QueueHandle_t search_queue;

// Функции для работы с системой поиска карт
void init_spiffs(void);
void generate_data_if_needed(void);
void load_indices(void);
void print_storage_info(void);
void show_random_cards(int count);
void print_index_table(void);
void search_card(uint64_t target_hex);

// Функции для многопоточности
void start_search_task(void);
void add_card_to_search_queue(uint64_t card_hex);

// Функции для тестовых карт
void add_test_cards_to_database(void);
void print_test_cards_info(void);

#ifdef __cplusplus
}
#endif

#endif // SEARCH_H