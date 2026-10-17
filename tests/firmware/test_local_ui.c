#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "../../firmware/src/local_ui.c"

static sampler_local_current_t mock_snapshot;
static bool mock_snapshot_ready;
static unsigned mock_snapshot_reads;
static unsigned mock_snapshot_resets;
static uint32_t mock_voltage_mv = 2500u;
static absolute_time_t mock_now;
static char displayed_current[OLED_TEXT_COLS + 1u];
static char displayed_feedback[OLED_TEXT_COLS + 1u];
static bool mock_pressed;
static int16_t mock_delta;
static bool mock_click;
static bool mock_long_press;
static bool mock_zero_result;
static unsigned mock_zero_calls;
static unsigned mock_tone_calls;
static uint32_t mock_tone_frequency;
static uint32_t mock_tone_duration;
static uint16_t mock_dac_mv = 2500u;

uint32_t adc_voltage_read_input_mv(uint16_t *raw_out, uint32_t *pin_mv_out) {
    (void)raw_out;
    (void)pin_mv_out;
    return mock_voltage_mv;
}

bool sampler_get_local_current(sampler_local_current_t *out) {
    ++mock_snapshot_reads;
    if (!mock_snapshot_ready) {
        return false;
    }
    *out = mock_snapshot;
    return true;
}
void sampler_reset_local_current(void) { ++mock_snapshot_resets; }
bool sampler_zero_local_current(void) {
    ++mock_zero_calls;
    if (!mock_zero_result) {
        return false;
    }
    mock_snapshot.zero_code = (uint16_t)((mock_snapshot.code_sum +
                                         mock_snapshot.sample_count / 2u) /
                                        mock_snapshot.sample_count);
    mock_snapshot_ready = false; /* Successful zero clears the averaging window. */
    return true;
}

/* The sampler owns the ADC. Drawing the OLED must never sample or change it. */
bool ads8866_try_read_sample(uint16_t *sample_out) {
    (void)sample_out;
    assert(!"OLED must consume the sampler snapshot without reading the ADC");
    return false;
}
ads8866_range_t ads8866_get_range(void) {
    assert(!"OLED must use the range carried by the sampler snapshot");
    return ADS8866_RANGE_10_OHM;
}
void ads8866_set_range(ads8866_range_t range) {
    (void)range;
    assert(!"OLED must not change the ADC range");
}

void buzzer_init(void) {}
void buzzer_tone(uint32_t freq_hz, uint32_t duration_ms) {
    ++mock_tone_calls;
    mock_tone_frequency = freq_hz;
    mock_tone_duration = duration_ms;
}
void buzzer_update(uint32_t now_ms) { (void)now_ms; }
void encoder_init(void) {}
void encoder_update(uint32_t now_ms) { (void)now_ms; }
int16_t encoder_get_delta(void) {
    int16_t delta = mock_delta;
    mock_delta = 0;
    return delta;
}
bool encoder_get_click(void) {
    bool clicked = mock_click;
    mock_click = false;
    return clicked;
}
bool encoder_get_long_press(void) {
    bool pressed = mock_long_press;
    mock_long_press = false;
    return pressed;
}
bool encoder_is_pressed(void) { return mock_pressed; }
uint16_t power_output_vdac_mv(void) { return mock_dac_mv; }
bool power_output_set_vdac_mv(uint16_t mv) { mock_dac_mv = mv; return true; }

bool oled_init(void) { return true; }
void oled_clear(void) {}
void oled_clear_line(uint8_t page) {
    if (page == 7u) {
        displayed_feedback[0] = '\0';
    }
}
void oled_write_line(uint8_t page, const char *text) {
    if (page == 7u) {
        snprintf(displayed_feedback, sizeof(displayed_feedback), "%s", text);
    }
}
void oled_write_line2x(uint8_t page, const char *text) {
    if (page == 5u) {
        snprintf(displayed_current, sizeof(displayed_current), "%s", text);
    }
}
void oled_write_line2x_with_right_text(uint8_t page, const char *text, const char *right_text) {
    (void)page;
    (void)text;
    (void)right_text;
}
void oled_write_frame(const uint8_t *frame) { (void)frame; }
uint8_t oled_glyph_column(char c, uint8_t col) { (void)c; (void)col; return 0u; }

absolute_time_t get_absolute_time(void) { return mock_now; }
uint32_t to_ms_since_boot(absolute_time_t time) { return (uint32_t)(time / 1000u); }
absolute_time_t delayed_by_ms(absolute_time_t time, uint32_t ms) {
    return time + (absolute_time_t)ms * 1000u;
}
void sleep_until(absolute_time_t target) { mock_now = target; }

static void set_mean_delta_q8(int64_t delta_q8) {
    /* 256 samples represent the fractional ADC mean exactly, without floats. */
    mock_snapshot.sample_count = 256u;
    mock_snapshot.code_sum = (uint64_t)((int64_t)mock_snapshot.zero_code * 256 + delta_q8);
}

static void reset_current(void) {
    mock_snapshot = (sampler_local_current_t){
        .code_sum = 32768u * 256u,
        .sample_count = 256u,
        .age_us = 0u,
        .range = ADS8866_RANGE_10_OHM,
        .zero_code = 32768u,
    };
    mock_snapshot_ready = true;
    mock_snapshot_reads = 0u;
    mock_snapshot_resets = 0u;
    g_sense_1_mohm = DEFAULT_SENSE_1_MOHM;
    g_sense_10_mohm = DEFAULT_SENSE_10_MOHM;
    g_display_current_valid = false;
    g_display_current_ua_x100 = 0;
    g_current_filter_accumulator = 0;
    g_display_current_range = ADS8866_RANGE_10_OHM;
    g_display_current_zero = 0u;
    displayed_current[0] = '\0';
}

static void expect_current_text(int64_t ua_x100, const char *expected) {
    char text[OLED_TEXT_COLS + 1u];
    format_current_value(text, sizeof(text), ua_x100);
    assert(strcmp(text, expected) == 0);
    assert(strlen(text) <= 10u); /* Double-size renderer's capacity. */
}

static void test_fractional_adc_mean_and_microamps(void) {
    reset_current();
    const int64_t zero_q8 = 32768ll * 256;
    /* A single ADC count is 0.0762939453125 microamps at 10 ohms.
       Keeping a half count is necessary to display 0.04 rather than 0.08. */
    assert(code_to_microamps_x100(zero_q8, ADS8866_RANGE_10_OHM, 32768u) == 0);
    assert(code_to_microamps_x100(zero_q8 + 256, ADS8866_RANGE_10_OHM, 32768u) == 8);
    assert(code_to_microamps_x100(zero_q8 - 256, ADS8866_RANGE_10_OHM, 32768u) == -8);
    assert(code_to_microamps_x100(zero_q8 + 128, ADS8866_RANGE_10_OHM, 32768u) == 4);
    assert(code_to_microamps_x100(zero_q8 - 128, ADS8866_RANGE_10_OHM, 32768u) == -4);
    assert(code_to_microamps_x100(zero_q8 + 32768, ADS8866_RANGE_10_OHM, 32768u) == 977);
    assert(code_to_microamps_x100(zero_q8 - 32768, ADS8866_RANGE_10_OHM, 32768u) == -977);
    assert(code_to_microamps_x100(zero_q8 + 256, ADS8866_RANGE_1_OHM, 32768u) == 76);

    set_mean_delta_q8(128);
    draw_screen();
    assert(mock_snapshot_reads == 1u);
    assert(strcmp(displayed_current, "0.04UA") == 0);
    reset_current();
    set_mean_delta_q8(-128);
    draw_screen();
    assert(strcmp(displayed_current, "-0.04UA") == 0);
}

static void test_format_unit_boundaries(void) {
    static const struct {
        int64_t ua_x100;
        const char *text;
    } cases[] = {
        {0, "0.00UA"}, {1, "0.01UA"}, {-1, "-0.01UA"},
        {99, "0.99UA"}, {100, "1.00UA"}, {460, "4.60UA"},
        {99999, "999.99UA"}, {-99999, "-999.99UA"},
        {100000, "1.00MA"}, {-100000, "-1.00MA"},
        {100999, "1.00MA"},
        {99999999, "999.99MA"}, {-99999999, "-999.99MA"},
        {100000000, "1.00A"}, {-100000000, "-1.00A"},
        {4999923706LL, "49.99A"}, {-4999923706LL, "-49.99A"},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        expect_current_text(cases[i].ua_x100, cases[i].text);
    }
}

static void test_snapshot_mean_stays_at_4_60_microamps(void) {
    reset_current();
    /* A 1,000-sample rolling mean with delta 60.293 ADC counts is
       4.5999889 microamps. Rounding the mean to a whole ADC count loses it. */
    mock_snapshot.sample_count = 1000u;
    mock_snapshot.code_sum = 32768u * 1000u + 60293u;
    draw_screen();
    assert(g_display_current_valid);
    assert(strcmp(displayed_current, "4.60UA") == 0);

    /* +/-0.018 microamp changes in successive window means should not make
       the final digits alternate on every 200 ms screen refresh. */
    for (unsigned i = 0; i < 100u; ++i) {
        set_mean_delta_q8(15435 + ((i & 1u) ? 60 : -60));
        draw_screen();
        assert(strcmp(displayed_current, "4.60UA") == 0);
    }
}

static void test_hysteresis_does_not_lock_out_persistent_change(void) {
    for (int sign = -1; sign <= 1; sign += 2) {
        reset_current();
        assert(filtered_current_ua_x100() == 0);
        /* 128 / 256 ADC count is +/-0.04 microamps, within the display
           deadband. The filter must still absorb every snapshot. */
        set_mean_delta_q8(sign * 128);
        for (unsigned i = 0; i < 64u; ++i) {
            assert(filtered_current_ua_x100() == 0);
        }
        assert(sign * g_current_filter_accumulator >= 3 * 256);

        /* An ongoing ramp crosses the deadband and keeps updating. */
        for (int64_t delta_q8 = 128; delta_q8 <= 8192; delta_q8 += 16) {
            set_mean_delta_q8(sign * delta_q8);
            (void)filtered_current_ua_x100();
        }
        assert(sign * g_display_current_ua_x100 >= 235);
        assert(sign * g_display_current_ua_x100 <= 244);
        set_mean_delta_q8(sign * 8192);
        for (unsigned i = 0; i < 64u; ++i) {
            (void)filtered_current_ua_x100();
        }
        /* 32 counts is 2.44140625 microamps. Display can hold within 0.04. */
        assert(sign * g_display_current_ua_x100 >= 240);
        assert(sign * g_display_current_ua_x100 <= 244);
    }
}

static void test_q8_filter_converges_below_display_deadband(void) {
    assert(filter_signed_iir(0, 8, 2u) == 2);
    assert(filter_signed_iir(0, -8, 2u) == -2);
    for (int sign = -1; sign <= 1; sign += 2) {
        int64_t accumulator = 0;
        assert(filter_display_measurement(0, &accumulator,
                                          OLED_CURRENT_FILTER_SHIFT, true) == 0);
        int64_t value = 0;
        for (unsigned i = 0; i < 64u; ++i) {
            value = filter_display_measurement(sign, &accumulator,
                                               OLED_CURRENT_FILTER_SHIFT, false);
        }
        assert(value == sign);
        for (unsigned i = 0; i < 64u; ++i) {
            value = filter_display_measurement(0, &accumulator,
                                               OLED_CURRENT_FILTER_SHIFT, false);
        }
        assert(value == 0);
    }
}

static void test_snapshot_calibration_changes_reset_filter(void) {
    reset_current();
    set_mean_delta_q8(256);
    assert(filtered_current_ua_x100() == 8);
    set_mean_delta_q8(512);
    (void)filtered_current_ua_x100();
    assert(g_current_filter_accumulator != 8 * 256);

    mock_snapshot.range = ADS8866_RANGE_1_OHM;
    set_mean_delta_q8(256);
    assert(filtered_current_ua_x100() == 76);
    assert(g_current_filter_accumulator == 76 * 256);

    /* Snapshot sum stays fixed while its calibration zero changes. */
    mock_snapshot.zero_code = 32767u;
    assert(filtered_current_ua_x100() == 153);
    assert(g_current_filter_accumulator == 153 * 256);

    local_ui_set_sense_milliohm(ADS8866_RANGE_1_OHM, 2000u);
    assert(mock_snapshot_resets == 1u);
    assert(!g_display_current_valid);
    assert(filtered_current_ua_x100() == 76);
    assert(g_current_filter_accumulator == 76 * 256);
}

static void test_invalid_snapshot_hides_old_current_and_recovers(void) {
    reset_current();
    set_mean_delta_q8(15435);
    draw_screen();
    assert(strcmp(displayed_current, "4.60UA") == 0);

    mock_snapshot_ready = false;
    draw_screen();
    assert(!g_display_current_valid);
    assert(strcmp(displayed_current, "--.--UA") == 0);

    mock_snapshot_ready = true;
    set_mean_delta_q8(-32768);
    draw_screen();
    assert(g_display_current_valid);
    assert(strcmp(displayed_current, "-9.77UA") == 0);
    assert(g_current_filter_accumulator == -977 * 256);

    mock_snapshot.sample_count = 0u;
    draw_screen();
    assert(!g_display_current_valid);
    assert(strcmp(displayed_current, "--.--UA") == 0);
    set_mean_delta_q8(256);
    draw_screen();
    assert(strcmp(displayed_current, "0.08UA") == 0);
}

static void test_voltage_small_step_converges(void) {
    g_display_voltage_valid = false;
    g_voltage_filter_accumulator = 0;
    mock_voltage_mv = 2500u;
    assert(filtered_output_voltage_mv() == 2500u);
    mock_voltage_mv = 2508u;
    assert(filtered_output_voltage_mv() == 2501u);

    g_display_voltage_valid = false;
    mock_voltage_mv = 2500u;
    assert(filtered_output_voltage_mv() == 2500u);
    mock_voltage_mv = 2501u;
    assert(filtered_output_voltage_mv() == 2500u);
    for (unsigned i = 0; i < 64u; ++i) {
        (void)filtered_output_voltage_mv();
    }
    assert(g_display_voltage_mv == 2501u);
    mock_voltage_mv = 2500u;
}

static void test_minimum_configured_shunt_uses_64_bits(void) {
    reset_current();
    local_ui_set_sense_milliohm(ADS8866_RANGE_1_OHM, 1u);
    /* Full-scale difference at 1 milliohm is 49.999237060546875 A. */
    const int64_t full_scale = 4999923706LL;
    assert(full_scale > INT32_MAX);
    assert(code_to_microamps_x100(65535ll * 256, ADS8866_RANGE_1_OHM, 0u) == full_scale);
    assert(code_to_microamps_x100(0, ADS8866_RANGE_1_OHM, 65535u) == -full_scale);
    assert(filter_signed_iir(full_scale, -full_scale, 2u) == 2499961853LL);
    assert(filter_signed_iir(-full_scale, full_scale, 2u) == -2499961853LL);

    mock_snapshot.range = ADS8866_RANGE_1_OHM;
    mock_snapshot.zero_code = 65535u;
    mock_snapshot.code_sum = 0u;
    assert(filtered_current_ua_x100() == -full_scale);
    assert(g_current_filter_accumulator == -full_scale * 256);
    expect_current_text(g_display_current_ua_x100, "-49.99A");
}

static void update_ui_at(uint32_t now_ms) {
    mock_now = (absolute_time_t)now_ms * 1000u;
    local_ui_update(now_ms);
}

static uint32_t reset_interaction(void) {
    reset_current();
    mock_pressed = false;
    mock_delta = 0;
    mock_click = false;
    mock_long_press = false;
    mock_zero_result = true;
    mock_zero_calls = 0u;
    mock_tone_calls = 0u;
    mock_tone_frequency = 0u;
    mock_tone_duration = 0u;
    mock_dac_mv = 2500u;
    g_step_index = 1u;
    g_press_turn_used = false;
    g_output_save_pending = false;
    g_settings_save_request = false;
    g_output_save_due_ms = 0u;
    g_next_refresh_ms = 0u;
    /* Move past earlier feedback deadlines without knowing their internals. */
    mock_now += 10000000u;
    displayed_feedback[0] = '\0';
    return to_ms_since_boot(mock_now);
}

static void test_long_press_zero_requests_one_settings_save(void) {
    uint32_t now = reset_interaction();
    set_mean_delta_q8(15435);
    draw_screen();
    assert(g_display_current_valid);

    mock_pressed = true;
    mock_long_press = true;
    update_ui_at(now);
    assert(mock_zero_calls == 1u);
    assert(!g_display_current_valid);
    assert(strcmp(displayed_current, "--.--UA") == 0);
    assert(strcmp(displayed_feedback, "ZERO OK") == 0);
    assert(mock_tone_calls == 1u);
    assert(mock_tone_frequency == 2200u);
    assert(mock_tone_duration == 120u);
    assert(g_step_index == 1u);
    assert(g_press_turn_used);
    assert(local_ui_take_settings_save_request());
    assert(!local_ui_take_settings_save_request());

    /* One physical hold cannot generate another zero/save before release. */
    mock_long_press = true;
    update_ui_at(now + 10u);
    assert(mock_zero_calls == 1u);
    assert(mock_tone_calls == 1u);
    assert(!local_ui_take_settings_save_request());
    g_next_refresh_ms = 0u;
    update_ui_at(now + 1499u);
    assert(strcmp(displayed_feedback, "ZERO OK") == 0);
    g_next_refresh_ms = 0u;
    update_ui_at(now + 1500u);
    assert(displayed_feedback[0] == '\0');
}

static void test_failed_long_press_does_not_save_or_change_step(void) {
    uint32_t now = reset_interaction();
    set_mean_delta_q8(15435);
    draw_screen();
    mock_zero_result = false;
    mock_pressed = true;
    mock_long_press = true;
    update_ui_at(now);
    assert(mock_zero_calls == 1u);
    assert(g_display_current_valid);
    assert(strcmp(displayed_current, "4.60UA") == 0);
    assert(strcmp(displayed_feedback, "ZERO WAIT") == 0);
    assert(mock_tone_calls == 1u);
    assert(mock_tone_frequency == 500u);
    assert(mock_tone_duration == 80u);
    assert(g_step_index == 1u);
    assert(g_press_turn_used);
    assert(!local_ui_take_settings_save_request());
    g_next_refresh_ms = 0u;
    update_ui_at(now + 1500u);
    assert(displayed_feedback[0] == '\0');
}

static void test_press_turn_suppresses_zero_and_release_click(void) {
    uint32_t now = reset_interaction();
    mock_pressed = true;
    mock_delta = -1;
    mock_long_press = true; /* Same update: rotation must be handled first. */
    update_ui_at(now);
    assert(mock_dac_mv == 1500u); /* 100 mV step with the existing 10x hold. */
    assert(g_step_index == 1u);
    assert(g_press_turn_used);
    assert(mock_zero_calls == 0u);
    assert(g_output_save_pending);
    assert(!local_ui_take_settings_save_request());

    mock_pressed = false;
    mock_click = true;
    update_ui_at(now + 10u);
    assert(g_step_index == 1u);
    assert(!g_press_turn_used);
    assert(mock_zero_calls == 0u);
}

static void test_long_release_allows_later_short_click(void) {
    for (unsigned zero_succeeds = 0u; zero_succeeds <= 1u; ++zero_succeeds) {
        for (unsigned release_sends_click = 0u; release_sends_click <= 1u;
             ++release_sends_click) {
            uint32_t now = reset_interaction();
            mock_zero_result = zero_succeeds != 0u;
            mock_pressed = true;
            mock_long_press = true;
            update_ui_at(now);
            assert(mock_zero_calls == 1u);
            assert(g_step_index == 1u);
            assert(local_ui_take_settings_save_request() == (zero_succeeds != 0u));

            mock_pressed = false;
            mock_click = release_sends_click != 0u;
            update_ui_at(now + 10u);
            assert(g_step_index == 1u);
            assert(!g_press_turn_used);
            assert(!local_ui_take_settings_save_request());

            mock_click = true;
            update_ui_at(now + 20u);
            assert(g_step_index == 2u);
            assert(mock_zero_calls == 1u);
            assert(mock_tone_frequency == 1800u);
            assert(mock_tone_duration == 35u);
        }
    }
}

static void test_dac_delay_uses_general_settings_save_request(void) {
    uint32_t now = reset_interaction();
    mock_delta = -1;
    update_ui_at(now);
    assert(mock_dac_mv == 2400u);
    assert(g_output_save_pending);
    assert(!local_ui_take_settings_save_request());
    update_ui_at(now + 1499u);
    assert(!local_ui_take_settings_save_request());
    update_ui_at(now + 1500u);
    assert(!g_output_save_pending);
    assert(local_ui_take_settings_save_request());
    assert(!local_ui_take_settings_save_request());
    assert(mock_zero_calls == 0u);
    assert(g_step_index == 1u);
}

int main(void) {
    test_fractional_adc_mean_and_microamps();
    test_format_unit_boundaries();
    test_snapshot_mean_stays_at_4_60_microamps();
    test_hysteresis_does_not_lock_out_persistent_change();
    test_q8_filter_converges_below_display_deadband();
    test_snapshot_calibration_changes_reset_filter();
    test_invalid_snapshot_hides_old_current_and_recovers();
    test_voltage_small_step_converges();
    test_minimum_configured_shunt_uses_64_bits();
    test_long_press_zero_requests_one_settings_save();
    test_failed_long_press_does_not_save_or_change_step();
    test_press_turn_suppresses_zero_and_release_click();
    test_long_release_allows_later_short_click();
    test_dac_delay_uses_general_settings_save_request();
    puts("local UI: snapshot means, hysteresis, calibration/invalid resets, Q8 extremes, long-press zero and settings saves passed");
    return 0;
}
