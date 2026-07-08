#include "encoder.h"

#include "board.h"
#include "buzzer.h"

#include "hardware/gpio.h"

#define ENCODER_DEBOUNCE_MS 20u
#define ENCODER_TRANSITIONS_PER_STEP 2

static int16_t g_encoder_delta = 0;
static int8_t g_encoder_accum = 0;
static uint8_t g_encoder_last_a = 0;

static bool g_key_stable_pressed = false;
static bool g_key_last_raw_pressed = false;
static bool g_key_click = false;
static uint32_t g_key_changed_ms = 0;

static uint8_t encoder_state(void) {
    uint8_t a = gpio_get(ENCODER_PIN_A) ? 1u : 0u;
    uint8_t b = gpio_get(ENCODER_PIN_B) ? 1u : 0u;
    return (uint8_t)((a << 1u) | b);
}

static void encoder_poll_rotation(void) {
    uint8_t a = gpio_get(ENCODER_PIN_A) ? 1u : 0u;
    if (a == g_encoder_last_a) {
        return;
    }
    g_encoder_last_a = a;

    uint8_t b = gpio_get(ENCODER_PIN_B) ? 1u : 0u;
    int8_t step = (a == b) ? -1 : 1;
    g_encoder_accum += step;
    if (g_encoder_accum >= ENCODER_TRANSITIONS_PER_STEP) {
        if (g_encoder_delta < INT16_MAX) {
            g_encoder_delta++;
        }
        g_encoder_accum = 0;
    } else if (g_encoder_accum <= -ENCODER_TRANSITIONS_PER_STEP) {
        if (g_encoder_delta > INT16_MIN) {
            g_encoder_delta--;
        }
        g_encoder_accum = 0;
    }
}

void encoder_init(void) {
    gpio_init(ENCODER_PIN_A);
    gpio_init(ENCODER_PIN_B);
    gpio_init(ENCODER_PIN_KEY);
    gpio_set_dir(ENCODER_PIN_A, GPIO_IN);
    gpio_set_dir(ENCODER_PIN_B, GPIO_IN);
    gpio_set_dir(ENCODER_PIN_KEY, GPIO_IN);
    gpio_pull_up(ENCODER_PIN_A);
    gpio_pull_up(ENCODER_PIN_B);
    gpio_pull_up(ENCODER_PIN_KEY);

    g_encoder_last_a = (uint8_t)(encoder_state() >> 1u);
    g_encoder_accum = 0;
    g_encoder_delta = 0;
    g_key_last_raw_pressed = !gpio_get(ENCODER_PIN_KEY);
    g_key_stable_pressed = g_key_last_raw_pressed;
    g_key_changed_ms = to_ms_since_boot(get_absolute_time());
}

void encoder_update(uint32_t now_ms) {
    encoder_poll_rotation();

    bool raw_pressed = !gpio_get(ENCODER_PIN_KEY);
    if (raw_pressed != g_key_last_raw_pressed) {
        g_key_last_raw_pressed = raw_pressed;
        g_key_changed_ms = now_ms;
    }

    if ((int32_t)(now_ms - g_key_changed_ms) < (int32_t)ENCODER_DEBOUNCE_MS) {
        return;
    }

    if (raw_pressed != g_key_stable_pressed) {
        g_key_stable_pressed = raw_pressed;
        if (raw_pressed) {
            buzzer_tone(1800, 25);
        } else {
            g_key_click = true;
            buzzer_tone(2300, 25);
        }
    }
}

int16_t encoder_get_delta(void) {
    int16_t delta = g_encoder_delta;
    g_encoder_delta = 0;

    if (delta != 0) {
        buzzer_tone(2800, 6);
    }

    return delta;
}

bool encoder_get_click(void) {
    bool click = g_key_click;
    g_key_click = false;
    return click;
}

bool encoder_is_pressed(void) {
    return g_key_stable_pressed;
}
