#ifndef WIEGAND_PROCESSOR_H
#define WIEGAND_PROCESSOR_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// External variables
extern uint64_t wiegand_data;
extern uint8_t wiegand_bit_count;
extern uint32_t wiegand_last_bit_time;
extern bool wiegand_data_ready;
extern uint32_t total_bits_received;
extern uint32_t card_read_count;

// Function declarations
void check_wiegand(void);
void handle_wiegand_bit(uint8_t bit);
void process_wiegand_data(void);
void reset_wiegand(void);
void speed_test(void);
void set_wiegand_debug(bool enable);  // Добавляем эту функцию

// Format-specific processors
void process_26bit_wiegand(void);
void process_34bit_wiegand(void);
void process_37bit_wiegand(void);
void process_56bit_wiegand(void);

#ifdef __cplusplus
}
#endif

#endif // WIEGAND_PROCESSOR_H