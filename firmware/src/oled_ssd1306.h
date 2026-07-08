#pragma once

#include <stdbool.h>
#include <stdint.h>

#define OLED_TEXT_COLS 21u

bool oled_init(void);
void oled_clear(void);
void oled_clear_line(uint8_t page);
void oled_write_line(uint8_t page, const char *text);
void oled_write_line2x(uint8_t page, const char *text);
