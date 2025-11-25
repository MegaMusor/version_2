#ifndef CARD_FORMATTER_H
#define CARD_FORMATTER_H

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

void format_serial_hex_7bytes(uint64_t data, uint8_t bit_count);

#ifdef __cplusplus
}
#endif

#endif // CARD_FORMATTER_H