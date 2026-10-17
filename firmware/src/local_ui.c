#include "local_ui.h"

#include "adc_voltage.h"
#include "ads8866.h"
#include "board.h"
#include "buzzer.h"
#include "encoder.h"
#include "oled_ssd1306.h"
#include "power_output.h"
#include "sampler.h"

#include "pico/stdlib.h"

#include <stdio.h>
#include <string.h>

#define UI_REFRESH_MS 200u
#define DAC_SAVE_DELAY_MS 1500u
#define ZERO_FEEDBACK_MS 1500u
#define STARTUP_ANIM_MS 3000u
#define STARTUP_FRAME_MS 20u
#define STEP_COUNT 3u
#define FAST_STEP_MULTIPLIER 10u
#define OLED_VOLTAGE_FILTER_SHIFT 3u
#define OLED_CURRENT_FILTER_SHIFT 2u
#define OLED_CURRENT_HYSTERESIS_UA_X100 5ll
#define OLED_FILTER_SCALE 256ll
#define FRONTEND_GAIN 50u
#define DEFAULT_SENSE_1_MOHM 1000u
#define DEFAULT_SENSE_10_MOHM 10000u

static uint32_t g_next_refresh_ms = 0;
static uint32_t g_output_save_due_ms = 0;
static uint8_t g_step_index = 1;
static bool g_press_turn_used = false;
static bool g_output_save_pending = false;
static bool g_settings_save_request = false;
typedef enum {
    ZERO_FEEDBACK_NONE = 0,
    ZERO_FEEDBACK_OK,
    ZERO_FEEDBACK_WAIT,
} zero_feedback_t;
static zero_feedback_t g_zero_feedback = ZERO_FEEDBACK_NONE;
static uint32_t g_zero_feedback_due_ms = 0;
static bool g_display_voltage_valid = false;
static uint32_t g_display_voltage_mv = 0;
static bool g_display_current_valid = false;
// Keep hundredths of a microamp through conversion and display filtering.
static int64_t g_display_current_ua_x100 = 0;
// Retain sub-unit fractions so stronger smoothing still converges at the last digit.
static int64_t g_voltage_filter_accumulator = 0;
static int64_t g_current_filter_accumulator = 0;
static ads8866_range_t g_display_current_range = ADS8866_RANGE_10_OHM;
static uint16_t g_display_current_zero = 0;
static uint32_t g_sense_1_mohm = DEFAULT_SENSE_1_MOHM;
static uint32_t g_sense_10_mohm = DEFAULT_SENSE_10_MOHM;
static uint8_t g_startup_frame[OLED_FRAME_BYTES];

static const uint16_t g_step_mv[STEP_COUNT] = {
    10u,
    100u,
    1000u,
};

typedef struct {
    uint16_t offset_ms;
    uint16_t freq_hz;
    uint16_t duration_ms;
} startup_note_t;

static const startup_note_t g_startup_notes[] = {
    {0u, 1568u, 45u},
    {110u, 2093u, 45u},
    {230u, 2637u, 55u},
    {380u, 3136u, 60u},
    {560u, 4186u, 95u},
    {2760u, 2637u, 120u},
};

static void startup_fb_clear(void) {
    memset(g_startup_frame, 0, sizeof(g_startup_frame));
}

static void startup_set_pixel(int16_t x, int16_t y, bool on) {
    if (x < 0 || y < 0 || x >= (int16_t)OLED_WIDTH || y >= (int16_t)OLED_HEIGHT) {
        return;
    }

    uint16_t index = (uint16_t)(((uint16_t)y >> 3u) * OLED_WIDTH + (uint16_t)x);
    uint8_t mask = (uint8_t)(1u << ((uint8_t)y & 7u));
    if (on) {
        g_startup_frame[index] |= mask;
    } else {
        g_startup_frame[index] &= (uint8_t)~mask;
    }
}

static void startup_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, bool on) {
    if (w <= 0 || h <= 0) {
        return;
    }

    for (int16_t yy = 0; yy < h; ++yy) {
        for (int16_t xx = 0; xx < w; ++xx) {
            startup_set_pixel((int16_t)(x + xx), (int16_t)(y + yy), on);
        }
    }
}

static void startup_hline(int16_t x, int16_t y, int16_t w, bool on) {
    startup_fill_rect(x, y, w, 1, on);
}

static void startup_vline(int16_t x, int16_t y, int16_t h, bool on) {
    startup_fill_rect(x, y, 1, h, on);
}

static void startup_rect(int16_t x, int16_t y, int16_t w, int16_t h, bool on) {
    if (w <= 0 || h <= 0) {
        return;
    }
    startup_hline(x, y, w, on);
    startup_hline(x, (int16_t)(y + h - 1), w, on);
    startup_vline(x, y, h, on);
    startup_vline((int16_t)(x + w - 1), y, h, on);
}

static uint8_t startup_text_len(const char *text) {
    uint8_t len = 0;
    while (text && text[len]) {
        ++len;
    }
    return len;
}

static int16_t startup_text_width(const char *text, uint8_t scale) {
    return (int16_t)startup_text_len(text) * 6 * (int16_t)scale;
}

static void startup_draw_char(int16_t x, int16_t y, char c, uint8_t scale, bool on) {
    if (scale == 0u) {
        return;
    }

    for (uint8_t col = 0; col < 5u; ++col) {
        uint8_t bits = oled_glyph_column(c, col);
        for (uint8_t row = 0; row < 7u; ++row) {
            if ((bits & (uint8_t)(1u << row)) == 0u) {
                continue;
            }
            startup_fill_rect((int16_t)(x + (int16_t)col * scale),
                              (int16_t)(y + (int16_t)row * scale),
                              scale,
                              scale,
                              on);
        }
    }
}

static void startup_draw_text(int16_t x, int16_t y, const char *text, uint8_t scale, bool on) {
    while (text && *text) {
        startup_draw_char(x, y, *text, scale, on);
        x = (int16_t)(x + 6 * (int16_t)scale);
        ++text;
    }
}

static void startup_draw_centered_text(int16_t y, const char *text, uint8_t scale, bool on) {
    int16_t x = (int16_t)(((int16_t)OLED_WIDTH - startup_text_width(text, scale)) / 2);
    startup_draw_text(x, y, text, scale, on);
}

static uint32_t startup_smoothstep(uint32_t elapsed_ms) {
    if (elapsed_ms > STARTUP_ANIM_MS) {
        elapsed_ms = STARTUP_ANIM_MS;
    }

    uint32_t t = (elapsed_ms * 1000u) / STARTUP_ANIM_MS;
    return (t * t * (3000u - 2u * t)) / 1000000u;
}

static void startup_draw_wave(uint32_t frame) {
    static const int8_t wave[] = {
        0, -1, -3, -4, -3, -1, 0, 1,
        3, 4, 3, 1, 0, -1, -2, -1,
    };

    startup_hline(92, 49, 5, true);
    startup_hline(118, 49, 4, true);
    for (uint8_t i = 0; i < 22u; ++i) {
        uint8_t idx = (uint8_t)((i + frame) & 15u);
        startup_set_pixel((int16_t)(97 + i), (int16_t)(49 + wave[idx]), true);
    }
}

static void startup_draw_frame(uint32_t elapsed_ms) {
    if (elapsed_ms > STARTUP_ANIM_MS) {
        elapsed_ms = STARTUP_ANIM_MS;
    }

    uint32_t frame = elapsed_ms / STARTUP_FRAME_MS;
    uint32_t eased = startup_smoothstep(elapsed_ms);
    int16_t title_x = (int16_t)(-68 + (int16_t)((102u * eased) / 1000u));
    int16_t fill_w = (int16_t)((elapsed_ms * 108u) / STARTUP_ANIM_MS);
    int16_t scan_x = (int16_t)(10 + (int16_t)((elapsed_ms * 104u) / STARTUP_ANIM_MS));

    startup_fb_clear();
    startup_draw_centered_text(1, "MICROSCOPE", 1u, true);
    startup_draw_text(title_x, 15, "POWER", 2u, true);
    startup_draw_centered_text(38, "ADS8866 WEBUSB", 1u, true);
    startup_draw_wave(frame);

    startup_rect(7, 55, 114, 6, true);
    startup_fill_rect(10, 57, fill_w, 2, true);
    startup_vline(scan_x, 54, 8, true);

    oled_write_frame(g_startup_frame);
}

static void startup_music_update(uint32_t elapsed_ms, uint8_t *note_index) {
    while (*note_index < (uint8_t)(sizeof(g_startup_notes) / sizeof(g_startup_notes[0])) &&
           elapsed_ms >= g_startup_notes[*note_index].offset_ms) {
        buzzer_tone(g_startup_notes[*note_index].freq_hz,
                    g_startup_notes[*note_index].duration_ms);
        ++(*note_index);
    }
}

static void startup_animation(void) {
    uint8_t note_index = 0;
    absolute_time_t start_time = get_absolute_time();
    uint32_t start_ms = to_ms_since_boot(start_time);

    while (true) {
        uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        uint32_t elapsed_ms = now_ms - start_ms;
        if (elapsed_ms > STARTUP_ANIM_MS) {
            elapsed_ms = STARTUP_ANIM_MS;
        }

        startup_music_update(elapsed_ms, &note_index);
        buzzer_update(now_ms);
        startup_draw_frame(elapsed_ms);

        if (elapsed_ms >= STARTUP_ANIM_MS) {
            break;
        }

        uint32_t next_elapsed =
            ((elapsed_ms / STARTUP_FRAME_MS) + 1u) * STARTUP_FRAME_MS;
        if (next_elapsed > STARTUP_ANIM_MS) {
            next_elapsed = STARTUP_ANIM_MS;
        }
        sleep_until(delayed_by_ms(start_time, next_elapsed));
    }

    buzzer_tone(0u, 0u);
    oled_clear();
}

static uint16_t clamp_dac_mv(int32_t mv) {
    if (mv < (int32_t)POWER_OUTPUT_VDAC_MIN_MV) {
        return POWER_OUTPUT_VDAC_MIN_MV;
    }
    if (mv > (int32_t)POWER_OUTPUT_VDAC_MAX_MV) {
        return POWER_OUTPUT_VDAC_MAX_MV;
    }
    return (uint16_t)mv;
}

static void format_voltage_value(char *out, uint32_t out_size, uint32_t mv) {
    uint32_t display_mv = mv > 999999u ? 999999u : mv;
    uint32_t display_cv = (display_mv + 5u) / 10u;
    snprintf(out,
             out_size,
             "%lu.%02luV",
             (unsigned long)(display_cv / 100u),
             (unsigned long)(display_cv % 100u));
}

static void format_step_value(char *out, uint32_t out_size) {
    switch (g_step_mv[g_step_index]) {
    case 10u:
        snprintf(out, out_size, "0.01");
        break;
    case 100u:
        snprintf(out, out_size, "0.1");
        break;
    case 1000u:
        snprintf(out, out_size, "1");
        break;
    default:
        snprintf(out, out_size, "%u", (unsigned)g_step_mv[g_step_index]);
        break;
    }
}

static void format_current_value(char *out, uint32_t out_size, int64_t ua_x100) {
    int64_t abs_ua_x100 = ua_x100 < 0 ? -ua_x100 : ua_x100;
    const char *sign = ua_x100 < 0 ? "-" : "";

    if (abs_ua_x100 >= 100000000ll) {
        snprintf(out,
                 out_size,
                 "%s%ld.%02ldA",
                 sign,
                 (long)(abs_ua_x100 / 100000000ll),
                 (long)((abs_ua_x100 % 100000000ll) / 1000000ll));
    } else if (abs_ua_x100 >= 100000ll) {
        snprintf(out,
                 out_size,
                 "%s%ld.%02ldMA",
                 sign,
                 (long)(abs_ua_x100 / 100000ll),
                 (long)((abs_ua_x100 % 100000ll) / 1000ll));
    } else {
        snprintf(out,
                 out_size,
                 "%s%ld.%02ldUA",
                 sign,
                 (long)(abs_ua_x100 / 100ll),
                 (long)(abs_ua_x100 % 100ll));
    }
}

static int64_t filter_signed_iir(int64_t previous, int64_t measured, uint8_t shift) {
    int64_t delta = measured - previous;
    int64_t half = 1ll << (shift - 1u);
    if (delta >= 0) {
        return previous + ((delta + half) >> shift);
    }
    return previous - (((-delta) + half) >> shift);
}

static int64_t filter_display_measurement(int64_t measured, int64_t *accumulator,
                                          uint8_t shift, bool reset) {
    int64_t target = measured * OLED_FILTER_SCALE;
    *accumulator = reset ? target : filter_signed_iir(*accumulator, target, shift);
    if (*accumulator >= 0) {
        return (*accumulator + OLED_FILTER_SCALE / 2) / OLED_FILTER_SCALE;
    }
    return -((-*accumulator + OLED_FILTER_SCALE / 2) / OLED_FILTER_SCALE);
}

static uint32_t filtered_output_voltage_mv(void) {
    uint32_t measured_mv = adc_voltage_read_input_mv(NULL, NULL);
    g_display_voltage_mv = (uint32_t)filter_display_measurement(
        measured_mv, &g_voltage_filter_accumulator, OLED_VOLTAGE_FILTER_SHIFT,
        !g_display_voltage_valid);
    g_display_voltage_valid = true;
    return g_display_voltage_mv;
}

static uint32_t sense_milliohm_for_range(ads8866_range_t range) {
    return range == ADS8866_RANGE_10_OHM ? g_sense_10_mohm : g_sense_1_mohm;
}

void local_ui_set_sense_milliohm(ads8866_range_t range, uint32_t milliohm) {
    if (milliohm == 0u) {
        return;
    }

    if (range == ADS8866_RANGE_10_OHM) {
        g_sense_10_mohm = milliohm;
    } else {
        g_sense_1_mohm = milliohm;
    }
    g_display_current_valid = false;
    sampler_reset_local_current();
}

uint32_t local_ui_get_sense_milliohm(ads8866_range_t range) {
    return sense_milliohm_for_range(range);
}

static int64_t code_to_microamps_x100(int64_t code_x256, ads8866_range_t range,
                                     uint16_t zero_code) {
    const int64_t delta_code_x256 = code_x256 - (int64_t)zero_code * OLED_FILTER_SCALE;
    const uint32_t sense_mohm = sense_milliohm_for_range(range);
    const int64_t denominator =
        (int64_t)65536 * (int64_t)FRONTEND_GAIN * (int64_t)sense_mohm * OLED_FILTER_SCALE;
    int64_t numerator =
        delta_code_x256 * (int64_t)ADS8866_REF_MV * 100000000ll;

    if (numerator >= 0) {
        numerator += denominator / 2;
    } else {
        numerator -= denominator / 2;
    }
    return numerator / denominator;
}

static int64_t filtered_current_ua_x100(void) {
    sampler_local_current_t current;
    if (!sampler_get_local_current(&current) || current.sample_count == 0u) {
        g_display_current_valid = false;
        return g_display_current_ua_x100;
    }

    // Average before converting, retaining fractional ADC codes without multiplying
    // a whole second's sum by the current conversion factor (which could overflow).
    int64_t code_x256 = (int64_t)((current.code_sum * OLED_FILTER_SCALE +
                                  current.sample_count / 2u) / current.sample_count);
    int64_t measured_ua_x100 =
        code_to_microamps_x100(code_x256, current.range, current.zero_code);

    bool reset = !g_display_current_valid ||
        current.range != g_display_current_range ||
        current.zero_code != g_display_current_zero;
    int64_t filtered_ua_x100 = filter_display_measurement(
        measured_ua_x100, &g_current_filter_accumulator, OLED_CURRENT_FILTER_SHIFT, reset);
    int64_t change = filtered_ua_x100 - g_display_current_ua_x100;
    if (reset || change >= OLED_CURRENT_HYSTERESIS_UA_X100 ||
        change <= -OLED_CURRENT_HYSTERESIS_UA_X100) {
        g_display_current_ua_x100 = filtered_ua_x100;
    }
    g_display_current_valid = true;
    g_display_current_range = current.range;
    g_display_current_zero = current.zero_code;
    return g_display_current_ua_x100;
}

static void draw_screen(void) {
    char line[OLED_TEXT_COLS + 1u];
    char step_line[6];
    uint32_t output_mv = filtered_output_voltage_mv();
    int64_t current_ua_x100 = filtered_current_ua_x100();

    oled_clear_line(0);
    format_voltage_value(line, sizeof(line), output_mv);
    format_step_value(step_line, sizeof(step_line));
    oled_write_line2x_with_right_text(1, line, step_line);
    oled_clear_line(3);
    oled_clear_line(4);
    if (g_display_current_valid) {
        format_current_value(line, sizeof(line), current_ua_x100);
    } else {
        snprintf(line, sizeof(line), "--.--UA");
    }
    oled_write_line2x(5, line);
    if (g_zero_feedback == ZERO_FEEDBACK_OK) {
        oled_write_line(7, "ZERO OK");
    } else if (g_zero_feedback == ZERO_FEEDBACK_WAIT) {
        oled_write_line(7, "ZERO WAIT");
    } else {
        oled_clear_line(7);
    }
}

void local_ui_init(void) {
    buzzer_init();
    if (oled_init()) {
        startup_animation();
    }
    encoder_init();
    draw_screen();
}

bool local_ui_take_settings_save_request(void) {
    if (!g_settings_save_request) {
        return false;
    }

    g_settings_save_request = false;
    return true;
}

void local_ui_update(uint32_t now_ms) {
    bool was_pressed = encoder_is_pressed();
    encoder_update(now_ms);
    buzzer_update(now_ms);

    if (g_zero_feedback != ZERO_FEEDBACK_NONE &&
        (int32_t)(now_ms - g_zero_feedback_due_ms) >= 0) {
        g_zero_feedback = ZERO_FEEDBACK_NONE;
        g_next_refresh_ms = 0;
    }

    int16_t delta = encoder_get_delta();
    if (delta != 0) {
        uint32_t step_mv = g_step_mv[g_step_index];
        if (was_pressed || encoder_is_pressed()) {
            step_mv *= FAST_STEP_MULTIPLIER;
            g_press_turn_used = true;
        }

        int32_t next_mv = (int32_t)power_output_vdac_mv() + (int32_t)delta * (int32_t)step_mv;
        uint16_t previous_mv = power_output_vdac_mv();
        uint16_t dac_mv = clamp_dac_mv(next_mv);
        if (power_output_set_vdac_mv(dac_mv)) {
            buzzer_tone(2600u, 8u);
            if (dac_mv != previous_mv) {
                g_output_save_pending = true;
                g_output_save_due_ms = now_ms + DAC_SAVE_DELAY_MS;
            }
        } else {
            buzzer_tone(500u, 80u);
        }
        g_next_refresh_ms = 0;
    }

    if (encoder_get_long_press() && encoder_is_pressed() && !g_press_turn_used) {
        g_press_turn_used = true;
        if (sampler_zero_local_current()) {
            g_display_current_valid = false;
            g_current_filter_accumulator = 0;
            g_settings_save_request = true;
            g_zero_feedback = ZERO_FEEDBACK_OK;
            buzzer_tone(2200u, 120u);
        } else {
            g_zero_feedback = ZERO_FEEDBACK_WAIT;
            buzzer_tone(500u, 80u);
        }
        g_zero_feedback_due_ms = now_ms + ZERO_FEEDBACK_MS;
        g_next_refresh_ms = 0;
    }

    if (encoder_get_click()) {
        if (!g_press_turn_used) {
            g_step_index = (uint8_t)((g_step_index + 1u) % STEP_COUNT);
            buzzer_tone(1800u, 35u);
        }
        g_next_refresh_ms = 0;
    }
    // A long press or a press-turn gesture may suppress the encoder click entirely.
    if (!encoder_is_pressed()) {
        g_press_turn_used = false;
    }

    if (g_output_save_pending && (int32_t)(now_ms - g_output_save_due_ms) >= 0) {
        g_output_save_pending = false;
        g_settings_save_request = true;
    }

    if (g_next_refresh_ms == 0u || (int32_t)(now_ms - g_next_refresh_ms) >= 0) {
        draw_screen();
        g_next_refresh_ms = now_ms + UI_REFRESH_MS;
    }
}
