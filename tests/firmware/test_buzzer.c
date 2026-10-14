#include <assert.h>
#include <stdio.h>
#include "pico/stdlib.h"

absolute_time_t get_absolute_time(void);
uint32_t to_ms_since_boot(absolute_time_t time);

#include "../../firmware/src/buzzer.c"

static uint32_t mock_now_ms;
static unsigned pin_function[30];
static unsigned pin_override[30];
static bool pwm_enabled;
static bool pwm_output_high;
static uint16_t pwm_compare;
static uint16_t pwm_counter;
static uint16_t pwm_wrap;
static unsigned unexpected_pin_high;
static unsigned pwm_output_transitions;
static bool forbid_high;

static bool buzzer_pin_high(void) {
    return pin_override[BUZZER_PIN] != GPIO_OVERRIDE_LOW && pwm_output_high;
}

static void check_pin(void) {
    if (forbid_high && buzzer_pin_high()) {
        ++unexpected_pin_high;
    }
}

uint pwm_gpio_to_slice_num(uint gpio) { return (gpio >> 1u) & 7u; }
uint pwm_gpio_to_channel(uint gpio) { return gpio & 1u; }

void gpio_set_function(unsigned pin, unsigned function) {
    pin_function[pin] = function;
    check_pin();
}

void gpio_set_outover(unsigned pin, unsigned value) {
    if (pin == BUZZER_PIN && value == GPIO_OVERRIDE_NORMAL) {
        assert(pwm_enabled && pwm_counter == 0u && pwm_compare > 0u);
        forbid_high = false;
    }
    pin_override[pin] = value;
    check_pin();
}

void pwm_set_enabled(uint slice, bool enabled) {
    assert(slice == pwm_gpio_to_slice_num(BUZZER_PIN));
    pwm_enabled = enabled;
    // RP2040 leaves the PWM pad state frozen when its counter is stopped.
    check_pin();
}

void pwm_set_counter(uint slice, uint16_t counter) {
    assert(slice == pwm_gpio_to_slice_num(BUZZER_PIN));
    pwm_counter = counter;
    check_pin();
}

void pwm_set_gpio_level(uint gpio, uint16_t level) {
    assert(gpio == BUZZER_PIN);
    pwm_set_chan_level(pwm_gpio_to_slice_num(gpio), pwm_gpio_to_channel(gpio), level);
}

void pwm_set_chan_level(uint slice, uint channel, uint16_t level) {
    assert(slice == pwm_gpio_to_slice_num(BUZZER_PIN));
    assert(channel == pwm_gpio_to_channel(BUZZER_PIN));
    pwm_compare = level;
    // CC writes do not clock a stopped output; HIGH can persist with CC == 0.
    check_pin();
}

void pwm_set_clkdiv_int_frac(uint slice, uint8_t integer, uint8_t fractional) {
    assert(slice == pwm_gpio_to_slice_num(BUZZER_PIN));
    assert(integer >= 1u && fractional < 16u);
    check_pin();
}

void pwm_set_wrap(uint slice, uint16_t wrap) {
    assert(slice == pwm_gpio_to_slice_num(BUZZER_PIN));
    pwm_wrap = wrap;
    check_pin();
}

absolute_time_t get_absolute_time(void) { return (absolute_time_t)mock_now_ms * 1000u; }
uint32_t to_ms_since_boot(absolute_time_t time) { return (uint32_t)(time / 1000u); }

static void advance_pwm(void) {
    assert(pwm_enabled);
    bool was_high = pwm_output_high;
    pwm_output_high = pwm_counter < pwm_compare;
    if (was_high != pwm_output_high) { ++pwm_output_transitions; }
    pwm_counter = pwm_counter == pwm_wrap ? 0u : (uint16_t)(pwm_counter + 1u);
}

static void start_tone_high(uint32_t duration_ms) {
    forbid_high = true;
    buzzer_tone(2000u, duration_ms);
    // Normal output is released only after the counter and duty are ready.
    forbid_high = false;
    assert(pwm_enabled && pin_override[BUZZER_PIN] == GPIO_OVERRIDE_NORMAL);
    advance_pwm();
    assert(buzzer_pin_high());
}

static void expect_stopped(void) {
    assert(!pwm_enabled && !buzzer_pin_high());
    assert(pin_override[BUZZER_PIN] == GPIO_OVERRIDE_LOW);
    assert(pwm_compare == 0u);
    assert(unexpected_pin_high == 0u);
    assert(pin_function[12] == 77u && pin_override[12] == 1u);
}

int main(void) {
    pin_function[12] = 77u;
    pin_override[12] = 1u;
    pwm_output_high = true;
    forbid_high = true;
    buzzer_init();
    forbid_high = false;
    assert(pin_function[BUZZER_PIN] == GPIO_FUNC_PWM);
    expect_stopped();

    // Reproduce the old stop defect: disabling and clearing CC leaves HIGH.
    pwm_output_high = true;
    pin_override[BUZZER_PIN] = GPIO_OVERRIDE_NORMAL;
    pwm_set_enabled(g_buzzer_slice, false);
    pwm_set_gpio_level(BUZZER_PIN, 0u);
    assert(buzzer_pin_high());
    buzzer_tone(0u, 0u);
    expect_stopped();

    mock_now_ms = 100u;
    start_tone_high(10u);
    buzzer_update(109u);
    assert(pwm_enabled && buzzer_pin_high());
    buzzer_update(110u);
    expect_stopped();

    // Restart from a stopped HIGH internal PWM state, then generate both phases.
    pwm_output_high = false;
    start_tone_high(20u);
    for (unsigned i = 0; i <= (unsigned)pwm_wrap + 1u; ++i) { advance_pwm(); }
    assert(pwm_output_transitions >= 2u);
    buzzer_tone(2000u, 0u);
    expect_stopped();

    // Retune while currently HIGH without exposing a stale HIGH during setup.
    pwm_output_high = false;
    start_tone_high(20u);
    // The override is released at tone start, so allow HIGH only on the final release.
    forbid_high = true;
    buzzer_tone(2400u, 30u);
    assert(pwm_enabled && pin_override[BUZZER_PIN] == GPIO_OVERRIDE_NORMAL);
    buzzer_tone(0u, 20u);
    expect_stopped();

    // The absolute millisecond deadline wraps, including the exact-zero deadline.
    mock_now_ms = UINT32_MAX - 9u;
    pwm_output_high = false;
    start_tone_high(10u);
    assert(g_buzzer_stop_ms == 0u);
    buzzer_update(UINT32_MAX);
    assert(pwm_enabled);
    buzzer_update(0u);
    expect_stopped();

    mock_now_ms = UINT32_MAX - 4u;
    pwm_output_high = false;
    start_tone_high(10u);
    buzzer_update(4u);
    assert(pwm_enabled);
    buzzer_update(5u);
    expect_stopped();

    puts("buzzer: frozen-HIGH stop, safe init/restart, PWM output, explicit/timed stop and deadline rollover passed");
    return 0;
}
