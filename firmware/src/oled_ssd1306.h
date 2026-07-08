#pragma once

#include <stdbool.h>
#include <stdint.h>

#define OLED_WIDTH 128u
#define OLED_HEIGHT 64u
#define OLED_PAGES 8u
#define OLED_TEXT_COLS 21u
#define OLED_FRAME_BYTES (OLED_WIDTH * OLED_PAGES)

bool oled_init(void);
void oled_clear(void);
void oled_clear_line(uint8_t page);
void oled_write_line(uint8_t page, const char *text);
void oled_write_line2x(uint8_t page, const char *text);
void oled_write_line2x_with_right_text(uint8_t page, const char *text, const char *right_text);
void oled_write_frame(const uint8_t *frame);
uint8_t oled_glyph_column(char c, uint8_t col);
