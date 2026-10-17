#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../firmware/src/encoder.c"

static bool mock_pins[30];
static uint32_t mock_now_ms;
static unsigned mock_tone_count;
static uint32_t mock_last_tone;

void gpio_init(unsigned pin) { assert(pin < 30u); }
void gpio_set_dir(unsigned pin, bool out) { assert(pin < 30u && out == GPIO_IN); }
void gpio_pull_up(unsigned pin) { assert(pin < 30u); }
bool gpio_get(unsigned pin) { assert(pin < 30u); return mock_pins[pin]; }
absolute_time_t get_absolute_time(void) { return (absolute_time_t)mock_now_ms * 1000u; }
uint32_t to_ms_since_boot(absolute_time_t time) { return (uint32_t)(time / 1000u); }
void buzzer_tone(uint32_t freq_hz, uint32_t duration_ms) {
    assert(duration_ms == 25u || duration_ms == 6u);
    mock_last_tone = freq_hz;
    ++mock_tone_count;
}

static void update_at(uint32_t now_ms) {
    mock_now_ms = now_ms;
    encoder_update(now_ms);
}

static void reset_encoder(uint32_t now_ms, bool pressed) {
    memset(mock_pins, 1, sizeof(mock_pins));
    mock_pins[ENCODER_PIN_KEY] = !pressed;
    mock_now_ms = now_ms;
    mock_tone_count = 0u;
    mock_last_tone = 0u;
    encoder_init();
    assert(encoder_is_pressed() == pressed);
    assert(!encoder_get_click() && !encoder_get_long_press());
    assert(encoder_get_delta() == 0);
}

static uint32_t press_at(uint32_t start_ms) {
    mock_pins[ENCODER_PIN_KEY] = false;
    update_at(start_ms);
    assert(!encoder_is_pressed());
    update_at(start_ms + 19u);
    assert(!encoder_is_pressed());
    update_at(start_ms + 20u);
    assert(encoder_is_pressed());
    return start_ms + 20u;
}

static void release_at(uint32_t start_ms) {
    mock_pins[ENCODER_PIN_KEY] = true;
    update_at(start_ms);
    assert(encoder_is_pressed());
    update_at(start_ms + 19u);
    assert(encoder_is_pressed());
    update_at(start_ms + 20u);
    assert(!encoder_is_pressed());
}

static void test_short_click_and_debounce(void) {
    reset_encoder(0u, false);
    mock_pins[ENCODER_PIN_KEY] = false;
    update_at(100u);
    mock_pins[ENCODER_PIN_KEY] = true;
    update_at(110u);
    update_at(130u);
    assert(!encoder_is_pressed() && !encoder_get_click());
    assert(mock_tone_count == 0u);

    uint32_t pressed_ms = press_at(200u);
    assert(mock_tone_count == 1u && mock_last_tone == 1800u);
    update_at(pressed_ms + 500u);
    assert(!encoder_get_click() && !encoder_get_long_press());

    /* Release bounce must not emit a click or restart the hold timer. */
    mock_pins[ENCODER_PIN_KEY] = true;
    update_at(800u);
    mock_pins[ENCODER_PIN_KEY] = false;
    update_at(810u);
    update_at(830u);
    assert(encoder_is_pressed() && !encoder_get_click());
    release_at(900u);
    assert(encoder_get_click() && !encoder_get_click());
    assert(!encoder_get_long_press());
    assert(mock_tone_count == 2u && mock_last_tone == 2300u);
}

static void test_long_press_threshold_once_and_release(void) {
    reset_encoder(0u, false);
    uint32_t pressed_ms = press_at(100u);
    update_at(pressed_ms + 1999u);
    assert(!encoder_get_long_press() && !encoder_get_click());
    update_at(pressed_ms + 2000u);
    assert(encoder_get_long_press() && !encoder_get_long_press());
    for (unsigned i = 0; i < 8u; ++i) {
        update_at(pressed_ms + 2000u);
        assert(!encoder_get_long_press());
    }
    update_at(pressed_ms + 100000u);
    assert(!encoder_get_long_press());
    release_at(pressed_ms + 100001u);
    assert(!encoder_get_click() && !encoder_get_long_press());
    assert(mock_tone_count == 1u); /* Long-press result feedback belongs to UI. */

    /* A new press after release must be independently armed. */
    pressed_ms = press_at(200000u);
    update_at(pressed_ms + 2000u);
    release_at(pressed_ms + 2001u);
    assert(!encoder_get_click());
    assert(encoder_get_long_press() && !encoder_get_long_press());

    /* A short raw release bounce at the threshold delays the notification
       until stable again, without restarting the original 2 s hold. */
    pressed_ms = press_at(300000u);
    mock_pins[ENCODER_PIN_KEY] = true;
    update_at(pressed_ms + 1999u);
    mock_pins[ENCODER_PIN_KEY] = false;
    update_at(pressed_ms + 2000u);
    update_at(pressed_ms + 2019u);
    assert(!encoder_get_long_press());
    update_at(pressed_ms + 2020u);
    assert(encoder_get_long_press());
    release_at(pressed_ms + 2021u);
    assert(!encoder_get_click());
}

static void test_partial_and_full_rotation_cancel_button_actions(void) {
    reset_encoder(0u, false);
    uint32_t pressed_ms = press_at(100u);
    /* A single A edge, even without a full detent/delta, cancels this hold. */
    mock_pins[ENCODER_PIN_A] = false;
    update_at(pressed_ms + 50u);
    assert(encoder_get_delta() == 0);
    update_at(pressed_ms + 2000u);
    assert(!encoder_get_long_press());
    release_at(pressed_ms + 2001u);
    assert(!encoder_get_click());

    reset_encoder(0u, false);
    pressed_ms = press_at(100u);
    mock_pins[ENCODER_PIN_A] = false;
    mock_pins[ENCODER_PIN_B] = true;
    update_at(pressed_ms + 1u);
    mock_pins[ENCODER_PIN_A] = true;
    mock_pins[ENCODER_PIN_B] = false;
    update_at(pressed_ms + 2u);
    assert(encoder_get_delta() == 1); /* Held-button adjustment is preserved. */
    assert(mock_last_tone == 2800u);
    update_at(pressed_ms + 2000u);
    assert(!encoder_get_long_press());
    release_at(pressed_ms + 2001u);
    assert(!encoder_get_click());
    pressed_ms = press_at(5000u);
    update_at(pressed_ms + 2000u);
    assert(encoder_get_long_press()); /* Rotation cancellation ends on release. */
}

static void test_rotation_during_press_debounce_also_cancels(void) {
    reset_encoder(0u, false);
    mock_pins[ENCODER_PIN_KEY] = false;
    mock_pins[ENCODER_PIN_A] = false;
    update_at(100u);
    update_at(120u);
    assert(encoder_is_pressed());
    update_at(2120u);
    assert(!encoder_get_long_press());
    release_at(2200u);
    assert(!encoder_get_click());

    /* A candidate cancelled by rotation and then bounced away must not
       poison a later clean press. */
    reset_encoder(0u, false);
    mock_pins[ENCODER_PIN_KEY] = false;
    mock_pins[ENCODER_PIN_A] = false;
    update_at(100u);
    mock_pins[ENCODER_PIN_KEY] = true;
    update_at(110u);
    update_at(130u);
    uint32_t pressed_ms = press_at(200u);
    update_at(pressed_ms + 2000u);
    assert(encoder_get_long_press());
}

static void test_initially_pressed_waits_for_release(void) {
    reset_encoder(100u, true);
    update_at(2100u);
    update_at(100000u);
    assert(!encoder_get_long_press() && !encoder_get_click());
    release_at(100001u);
    assert(!encoder_get_click());
    uint32_t pressed_ms = press_at(200000u);
    update_at(pressed_ms + 2000u);
    assert(encoder_get_long_press());

    /* Reinitialization discards unconsumed events from a previous session. */
    release_at(pressed_ms + 2001u);
    pressed_ms = press_at(300000u);
    update_at(pressed_ms + 2000u);
    encoder_init();
    assert(!encoder_get_long_press() && !encoder_get_click());
}

static void test_timer_wrap_for_debounce_and_hold(void) {
    reset_encoder(UINT32_MAX - 100u, false);
    uint32_t pressed_ms = press_at(UINT32_MAX - 10u);
    assert(pressed_ms == 9u); /* Debounce spans the timer wrap. */
    update_at(pressed_ms + 1999u);
    assert(!encoder_get_long_press());
    update_at(pressed_ms + 2000u);
    assert(encoder_get_long_press());
    release_at(pressed_ms + 2001u);
    assert(!encoder_get_click());

    reset_encoder(UINT32_MAX - 2000u, false);
    pressed_ms = press_at(UINT32_MAX - 1000u);
    update_at(pressed_ms + 1999u);
    assert(!encoder_get_long_press());
    update_at(pressed_ms + 2000u);
    assert(encoder_get_long_press()); /* The 2 s hold itself spans wrap. */
    release_at(pressed_ms + 2001u);
    assert(!encoder_get_click());

    reset_encoder(UINT32_MAX - 100u, false);
    (void)press_at(UINT32_MAX - 90u);
    release_at(UINT32_MAX - 10u); /* Release debounce itself spans wrap. */
    assert(encoder_get_click() && !encoder_get_click());
    assert(!encoder_get_long_press());
}

int main(void) {
    test_short_click_and_debounce();
    test_long_press_threshold_once_and_release();
    test_partial_and_full_rotation_cancel_button_actions();
    test_rotation_during_press_debounce_also_cancels();
    test_initially_pressed_waits_for_release();
    test_timer_wrap_for_debounce_and_hold();
    puts("encoder: debounce, short click, one-shot 2 s hold, rotation cancellation, boot hold and timer wrap passed");
    return 0;
}
