#include "local_ui.h"

#include "adc_voltage.h"
#include "ads8866.h"
#include "board.h"
#include "buzzer.h"
#include "encoder.h"
#include "oled_ssd1306.h"
#include "power_output.h"
#include "sampler.h"

#include <stdio.h>

#define UI_REFRESH_MS 200u
#define DAC_SAVE_DELAY_MS 1500u
#define STEP_COUNT 4u
#define FAST_STEP_MULTIPLIER 10u
#define OLED_VOLTAGE_FILTER_SHIFT 2u
#define OLED_CURRENT_FILTER_SHIFT 2u
#define FRONTEND_GAIN 50u
#define LOCAL_CURRENT_AVG_SAMPLES 8u
#define LOCAL_RANGE_RAIL_LOW_CODE 512u
#define LOCAL_RANGE_RAIL_HIGH_CODE 60000u
#define LOCAL_RANGE_10_TO_1_DELTA_CODE 30000u
#define LOCAL_RANGE_1_TO_10_DELTA_CODE 1800u
#define LOCAL_RANGE_SETTLE_READS 4u
#define DEFAULT_SENSE_1_MOHM 1000u
#define DEFAULT_SENSE_10_MOHM 10000u

static uint32_t g_next_refresh_ms = 0;
static uint32_t g_output_save_due_ms = 0;
static uint8_t g_step_index = 1;
static bool g_press_turn_used = false;
static bool g_output_save_pending = false;
static bool g_output_save_request = false;
static bool g_display_voltage_valid = false;
static uint32_t g_display_voltage_mv = 0;
static bool g_display_current_valid = false;
static int32_t g_display_current_ua = 0;
static ads8866_range_t g_display_current_range = ADS8866_RANGE_10_OHM;
static uint16_t g_display_current_zero = 0;
static uint32_t g_sense_1_mohm = DEFAULT_SENSE_1_MOHM;
static uint32_t g_sense_10_mohm = DEFAULT_SENSE_10_MOHM;

static const uint16_t g_step_mv[STEP_COUNT] = {
    10u,
    50u,
    100u,
    500u,
};

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
    snprintf(out,
             out_size,
             "%lu.%03luV",
             (unsigned long)(display_mv / 1000u),
             (unsigned long)(display_mv % 1000u));
}

static void format_current_value(char *out, uint32_t out_size, int32_t ua) {
    int32_t abs_ua = ua < 0 ? -ua : ua;
    const char *sign = ua < 0 ? "-" : "";

    if (abs_ua >= 1000000) {
        snprintf(out,
                 out_size,
                 "%s%ld.%02ldA",
                 sign,
                 (long)(abs_ua / 1000000),
                 (long)((abs_ua % 1000000) / 10000));
    } else if (abs_ua >= 1000) {
        snprintf(out,
                 out_size,
                 "%s%ld.%02ldMA",
                 sign,
                 (long)(abs_ua / 1000),
                 (long)((abs_ua % 1000) / 10));
    } else {
        snprintf(out, out_size, "%s%ldUA", sign, (long)abs_ua);
    }
}

static uint32_t filtered_output_voltage_mv(void) {
    uint32_t measured_mv = adc_voltage_read_input_mv(NULL, NULL);
    if (!g_display_voltage_valid) {
        g_display_voltage_mv = measured_mv;
        g_display_voltage_valid = true;
        return g_display_voltage_mv;
    }

    g_display_voltage_mv =
        ((g_display_voltage_mv * ((1u << OLED_VOLTAGE_FILTER_SHIFT) - 1u)) +
         measured_mv +
         (1u << (OLED_VOLTAGE_FILTER_SHIFT - 1u))) >>
        OLED_VOLTAGE_FILTER_SHIFT;
    return g_display_voltage_mv;
}

static uint16_t code_distance(uint16_t a, uint16_t b) {
    return a > b ? (uint16_t)(a - b) : (uint16_t)(b - a);
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
}

uint32_t local_ui_get_sense_milliohm(ads8866_range_t range) {
    return sense_milliohm_for_range(range);
}

static uint16_t read_ads_average(void) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i < LOCAL_CURRENT_AVG_SAMPLES; ++i) {
        sum += ads8866_read_sample();
    }
    return (uint16_t)((sum + (LOCAL_CURRENT_AVG_SAMPLES / 2u)) / LOCAL_CURRENT_AVG_SAMPLES);
}

static void settle_ads_reads(void) {
    for (uint32_t i = 0; i < LOCAL_RANGE_SETTLE_READS; ++i) {
        (void)ads8866_read_sample();
    }
}

static uint16_t read_current_code(ads8866_range_t *range_out) {
    ads8866_range_t range = ads8866_get_range();
    uint16_t code = read_ads_average();

    if (sampler_get_auto_range()) {
        uint16_t zero = sampler_get_zero_code_for_range(range);
        uint16_t delta = code_distance(code, zero);
        if (range == ADS8866_RANGE_10_OHM) {
            if (code >= LOCAL_RANGE_RAIL_HIGH_CODE ||
                code <= LOCAL_RANGE_RAIL_LOW_CODE ||
                delta >= LOCAL_RANGE_10_TO_1_DELTA_CODE) {
                ads8866_set_range(ADS8866_RANGE_1_OHM);
                settle_ads_reads();
                range = ADS8866_RANGE_1_OHM;
                code = read_ads_average();
            }
        } else if (delta <= LOCAL_RANGE_1_TO_10_DELTA_CODE &&
                   code < LOCAL_RANGE_RAIL_HIGH_CODE &&
                   code > LOCAL_RANGE_RAIL_LOW_CODE) {
            ads8866_set_range(ADS8866_RANGE_10_OHM);
            settle_ads_reads();
            range = ADS8866_RANGE_10_OHM;
            code = read_ads_average();
        }
    } else {
        range = sampler_get_range();
        if (ads8866_get_range() != range) {
            ads8866_set_range(range);
            settle_ads_reads();
        }
        code = read_ads_average();
    }

    if (range_out) {
        *range_out = range;
    }
    return code;
}

static int32_t code_to_microamps(uint16_t code, ads8866_range_t range) {
    const int32_t delta_code =
        (int32_t)code - (int32_t)sampler_get_zero_code_for_range(range);
    const uint32_t sense_mohm = sense_milliohm_for_range(range);
    const int64_t denominator =
        (int64_t)65536 * (int64_t)FRONTEND_GAIN * (int64_t)sense_mohm;
    int64_t numerator =
        (int64_t)delta_code * (int64_t)ADS8866_REF_MV * 1000000ll;

    if (numerator >= 0) {
        numerator += denominator / 2;
    } else {
        numerator -= denominator / 2;
    }
    return (int32_t)(numerator / denominator);
}

static int32_t filter_signed_iir(int32_t previous, int32_t measured, uint8_t shift) {
    int32_t delta = measured - previous;
    int32_t half = (int32_t)(1u << (shift - 1u));
    if (delta >= 0) {
        return previous + ((delta + half) >> shift);
    }
    return previous - (((-delta) + half) >> shift);
}

static int32_t filtered_current_ua(void) {
    ads8866_range_t range = ADS8866_RANGE_10_OHM;
    uint16_t code = read_current_code(&range);
    uint16_t zero = sampler_get_zero_code_for_range(range);
    int32_t measured_ua = code_to_microamps(code, range);

    if (!g_display_current_valid ||
        range != g_display_current_range ||
        zero != g_display_current_zero) {
        g_display_current_ua = measured_ua;
        g_display_current_valid = true;
        g_display_current_range = range;
        g_display_current_zero = zero;
        return g_display_current_ua;
    }

    g_display_current_ua =
        filter_signed_iir(g_display_current_ua, measured_ua, OLED_CURRENT_FILTER_SHIFT);
    return g_display_current_ua;
}

static void draw_screen(void) {
    char line[OLED_TEXT_COLS + 1u];
    uint32_t output_mv = filtered_output_voltage_mv();
    int32_t current_ua = filtered_current_ua();

    oled_clear_line(0);
    format_voltage_value(line, sizeof(line), output_mv);
    oled_write_line2x(1, line);
    oled_clear_line(3);
    oled_clear_line(4);
    format_current_value(line, sizeof(line), current_ua);
    oled_write_line2x(5, line);
    oled_clear_line(7);
}

void local_ui_init(void) {
    buzzer_init();
    encoder_init();
    oled_init();
    draw_screen();
}

bool local_ui_take_output_save_request(void) {
    if (!g_output_save_request) {
        return false;
    }

    g_output_save_request = false;
    return true;
}

void local_ui_update(uint32_t now_ms) {
    encoder_update(now_ms);
    buzzer_update(now_ms);

    if (encoder_get_click()) {
        if (g_press_turn_used) {
            g_press_turn_used = false;
            g_next_refresh_ms = 0;
            return;
        }
        g_step_index = (uint8_t)((g_step_index + 1u) % STEP_COUNT);
        buzzer_tone(1800u, 35u);
        g_next_refresh_ms = 0;
    }

    int16_t delta = encoder_get_delta();
    if (delta != 0) {
        uint32_t step_mv = g_step_mv[g_step_index];
        if (encoder_is_pressed()) {
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

    if (g_output_save_pending && (int32_t)(now_ms - g_output_save_due_ms) >= 0) {
        g_output_save_pending = false;
        g_output_save_request = true;
    }

    if (g_next_refresh_ms == 0u || (int32_t)(now_ms - g_next_refresh_ms) >= 0) {
        draw_screen();
        g_next_refresh_ms = now_ms + UI_REFRESH_MS;
    }
}
