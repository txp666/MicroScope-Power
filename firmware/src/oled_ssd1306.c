#include "oled_ssd1306.h"

#include "board.h"

#include "hardware/i2c.h"

#include <string.h>

#define OLED_ADDR 0x3cu
#define OLED_WIDTH 128u
#define OLED_PAGES 8u
#define OLED_CONTROLLER_X_OFFSET 2u
#define OLED_TEXT_X_OFFSET 2u
#define OLED_CMD 0x00u
#define OLED_DATA 0x40u
#define OLED_TIMEOUT_US 5000u

static char g_line_cache[OLED_PAGES][OLED_TEXT_COLS];
static bool g_cache_valid = false;

static bool oled_write_bytes(const uint8_t *data, uint32_t len) {
    int written = i2c_write_timeout_us(POWER_I2C_PORT, OLED_ADDR, data, len, false, OLED_TIMEOUT_US);
    return written == (int)len;
}

static bool oled_cmd3(uint8_t a, uint8_t b, uint8_t c) {
    uint8_t data[4] = {OLED_CMD, a, b, c};
    return oled_write_bytes(data, sizeof(data));
}

static uint8_t glyph_col(char c, uint8_t col) {
    static const uint8_t font_digits[10][5] = {
        {0x3e, 0x51, 0x49, 0x45, 0x3e},
        {0x00, 0x42, 0x7f, 0x40, 0x00},
        {0x42, 0x61, 0x51, 0x49, 0x46},
        {0x21, 0x41, 0x45, 0x4b, 0x31},
        {0x18, 0x14, 0x12, 0x7f, 0x10},
        {0x27, 0x45, 0x45, 0x45, 0x39},
        {0x3c, 0x4a, 0x49, 0x49, 0x30},
        {0x01, 0x71, 0x09, 0x05, 0x03},
        {0x36, 0x49, 0x49, 0x49, 0x36},
        {0x06, 0x49, 0x49, 0x29, 0x1e},
    };

    static const uint8_t font_upper[26][5] = {
        {0x7e, 0x11, 0x11, 0x11, 0x7e},
        {0x7f, 0x49, 0x49, 0x49, 0x36},
        {0x3e, 0x41, 0x41, 0x41, 0x22},
        {0x7f, 0x41, 0x41, 0x22, 0x1c},
        {0x7f, 0x49, 0x49, 0x49, 0x41},
        {0x7f, 0x09, 0x09, 0x09, 0x01},
        {0x3e, 0x41, 0x49, 0x49, 0x7a},
        {0x7f, 0x08, 0x08, 0x08, 0x7f},
        {0x00, 0x41, 0x7f, 0x41, 0x00},
        {0x20, 0x40, 0x41, 0x3f, 0x01},
        {0x7f, 0x08, 0x14, 0x22, 0x41},
        {0x7f, 0x40, 0x40, 0x40, 0x40},
        {0x7f, 0x02, 0x0c, 0x02, 0x7f},
        {0x7f, 0x04, 0x08, 0x10, 0x7f},
        {0x3e, 0x41, 0x41, 0x41, 0x3e},
        {0x7f, 0x09, 0x09, 0x09, 0x06},
        {0x3e, 0x41, 0x51, 0x21, 0x5e},
        {0x7f, 0x09, 0x19, 0x29, 0x46},
        {0x46, 0x49, 0x49, 0x49, 0x31},
        {0x01, 0x01, 0x7f, 0x01, 0x01},
        {0x3f, 0x40, 0x40, 0x40, 0x3f},
        {0x1f, 0x20, 0x40, 0x20, 0x1f},
        {0x3f, 0x40, 0x38, 0x40, 0x3f},
        {0x63, 0x14, 0x08, 0x14, 0x63},
        {0x07, 0x08, 0x70, 0x08, 0x07},
        {0x61, 0x51, 0x49, 0x45, 0x43},
    };

    if (col >= 5u) {
        return 0x00;
    }
    if (c >= '0' && c <= '9') {
        return font_digits[(uint8_t)(c - '0')][col];
    }
    if (c >= 'a' && c <= 'z') {
        c = (char)(c - 'a' + 'A');
    }
    if (c >= 'A' && c <= 'Z') {
        return font_upper[(uint8_t)(c - 'A')][col];
    }

    switch (c) {
    case ' ':
        return 0x00;
    case '.':
        return (col == 2u) ? 0x60 : 0x00;
    case ':':
        return (col == 2u) ? 0x36 : 0x00;
    case '>':
        return (col == 0u) ? 0x41 : (col == 1u) ? 0x22 : (col == 2u) ? 0x14 : (col == 3u) ? 0x08 : 0x00;
    case '<':
        return (col == 0u) ? 0x08 : (col == 1u) ? 0x14 : (col == 2u) ? 0x22 : (col == 3u) ? 0x41 : 0x00;
    case '-':
        return (col >= 1u && col <= 3u) ? 0x08 : 0x00;
    case '/':
        return (col == 0u) ? 0x20 : (col == 1u) ? 0x10 : (col == 2u) ? 0x08 : (col == 3u) ? 0x04 : 0x02;
    default:
        return 0x00;
    }
}

static bool oled_set_page(uint8_t page, uint8_t col) {
    col = (uint8_t)(col + OLED_CONTROLLER_X_OFFSET);
    return oled_cmd3((uint8_t)(0xb0u | page), (uint8_t)(col & 0x0fu), (uint8_t)(0x10u | (col >> 4u)));
}

static uint8_t expand_nibble_2x(uint8_t bits) {
    bits &= 0x0fu;
    bits = (uint8_t)((bits | (uint8_t)(bits << 2u)) & 0x33u);
    bits = (uint8_t)((bits | (uint8_t)(bits << 1u)) & 0x55u);
    return (uint8_t)(bits | (uint8_t)(bits << 1u));
}

bool oled_init(void) {
    static const uint8_t init[] = {
        OLED_CMD,
        0xae, 0xd5, 0x80, 0xa8, 0x3f, 0xd3, 0x00, 0x40,
        0x8d, 0x14, 0x20, 0x02, 0xa1, 0xc8, 0xda, 0x12,
        0x81, 0xcf, 0xd9, 0xf1, 0xdb, 0x40, 0xa4, 0xa6, 0xaf,
    };

    g_cache_valid = false;
    if (!oled_write_bytes(init, sizeof(init))) {
        return false;
    }
    oled_clear();
    return true;
}

void oled_clear(void) {
    uint8_t data[1 + OLED_WIDTH];
    data[0] = OLED_DATA;
    memset(data + 1, 0, OLED_WIDTH);
    bool ok = true;

    for (uint8_t page = 0; page < OLED_PAGES; ++page) {
        ok = oled_set_page(page, 0) && oled_write_bytes(data, sizeof(data)) && ok;
        memset(g_line_cache[page], ' ', OLED_TEXT_COLS);
    }
    g_cache_valid = ok;
}

void oled_clear_line(uint8_t page) {
    if (page >= OLED_PAGES) {
        return;
    }

    uint8_t data[1 + OLED_WIDTH];
    data[0] = OLED_DATA;
    memset(data + 1, 0, OLED_WIDTH);

    if (oled_set_page(page, 0) && oled_write_bytes(data, sizeof(data))) {
        memset(g_line_cache[page], ' ', OLED_TEXT_COLS);
        g_cache_valid = true;
    } else {
        g_cache_valid = false;
    }
}

void oled_write_line(uint8_t page, const char *text) {
    if (page >= OLED_PAGES) {
        return;
    }

    char line[OLED_TEXT_COLS];
    for (uint8_t i = 0; i < OLED_TEXT_COLS; ++i) {
        char c = ' ';
        if (text && text[i]) {
            c = text[i];
        }
        if (c < 32 || c > 126) {
            c = ' ';
        }
        line[i] = c;
    }

    if (g_cache_valid && memcmp(g_line_cache[page], line, OLED_TEXT_COLS) == 0) {
        return;
    }

    uint8_t data[1 + OLED_WIDTH];
    uint16_t pos = 0;
    data[pos++] = OLED_DATA;
    for (uint8_t i = 0; i < OLED_TEXT_X_OFFSET && pos < sizeof(data); ++i) {
        data[pos++] = 0x00;
    }
    for (uint8_t i = 0; i < OLED_TEXT_COLS && pos + 6u <= sizeof(data); ++i) {
        for (uint8_t col = 0; col < 5u; ++col) {
            data[pos++] = glyph_col(line[i], col);
        }
        data[pos++] = 0x00;
    }
    while (pos < sizeof(data)) {
        data[pos++] = 0x00;
    }

    oled_set_page(page, 0);
    if (oled_write_bytes(data, sizeof(data))) {
        memcpy(g_line_cache[page], line, OLED_TEXT_COLS);
        g_cache_valid = true;
    }
}

void oled_write_line2x(uint8_t page, const char *text) {
    if (page + 1u >= OLED_PAGES) {
        return;
    }

    uint8_t len = 0;
    while (text && text[len] && len < 10u) {
        ++len;
    }

    uint8_t left = (uint8_t)((OLED_WIDTH - (uint8_t)(len * 12u)) / 2u);
    for (uint8_t half = 0; half < 2u; ++half) {
        uint8_t data[1 + OLED_WIDTH];
        uint16_t pos = 0;
        data[pos++] = OLED_DATA;

        for (uint8_t i = 0; i < left && pos < sizeof(data); ++i) {
            data[pos++] = 0x00;
        }

        for (uint8_t i = 0; i < len && pos + 12u <= sizeof(data); ++i) {
            char c = text[i];
            for (uint8_t col = 0; col < 5u; ++col) {
                uint8_t bits = glyph_col(c, col);
                if (half == 0u) {
                    bits = expand_nibble_2x(bits);
                } else {
                    bits = expand_nibble_2x((uint8_t)(bits >> 4u));
                }
                data[pos++] = bits;
                data[pos++] = bits;
            }
            data[pos++] = 0x00;
            data[pos++] = 0x00;
        }

        while (pos < sizeof(data)) {
            data[pos++] = 0x00;
        }

        oled_set_page((uint8_t)(page + half), 0);
        oled_write_bytes(data, sizeof(data));
        memset(g_line_cache[page + half], 0, OLED_TEXT_COLS);
    }
}
