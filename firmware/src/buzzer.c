#include "buzzer.h"

#include "board.h"

#include "hardware/gpio.h"
#include "hardware/pwm.h"

static uint g_buzzer_slice = 0;
static uint g_buzzer_channel = 0;
static uint32_t g_buzzer_stop_ms = 0;
static bool g_buzzer_active = false;

static void buzzer_stop(void) {
    // A halted RP2040 PWM retains its last output level, even after CC is zeroed.
    gpio_set_outover(BUZZER_PIN, GPIO_OVERRIDE_LOW);
    pwm_set_enabled(g_buzzer_slice, false);
    pwm_set_gpio_level(BUZZER_PIN, 0);
    g_buzzer_stop_ms = 0;
    g_buzzer_active = false;
}

void buzzer_init(void) {
    gpio_set_outover(BUZZER_PIN, GPIO_OVERRIDE_LOW);
    gpio_set_function(BUZZER_PIN, GPIO_FUNC_PWM);
    g_buzzer_slice = pwm_gpio_to_slice_num(BUZZER_PIN);
    g_buzzer_channel = pwm_gpio_to_channel(BUZZER_PIN);
    buzzer_stop();
}

void buzzer_tone(uint32_t freq_hz, uint32_t duration_ms) {
    if (freq_hz == 0u || duration_ms == 0u) {
        buzzer_stop();
        return;
    }

    gpio_set_outover(BUZZER_PIN, GPIO_OVERRIDE_LOW);
    pwm_set_enabled(g_buzzer_slice, false);
    pwm_set_counter(g_buzzer_slice, 0);

    const uint32_t sys_hz = 125000000u;
    uint32_t divider16 = (sys_hz * 16u) / (freq_hz * 4096u);
    if (divider16 < 16u) {
        divider16 = 16u;
    }
    if (divider16 > 255u * 16u) {
        divider16 = 255u * 16u;
    }

    uint32_t wrap = (sys_hz * 16u) / (divider16 * freq_hz);
    if (wrap < 2u) {
        wrap = 2u;
    }
    wrap -= 1u;

    pwm_set_clkdiv_int_frac(g_buzzer_slice, (uint8_t)(divider16 / 16u), (uint8_t)(divider16 & 0x0fu));
    pwm_set_wrap(g_buzzer_slice, (uint16_t)wrap);
    pwm_set_chan_level(g_buzzer_slice, g_buzzer_channel, (uint16_t)(wrap / 2u));
    pwm_set_enabled(g_buzzer_slice, true);
    gpio_set_outover(BUZZER_PIN, GPIO_OVERRIDE_NORMAL);
    g_buzzer_stop_ms = to_ms_since_boot(get_absolute_time()) + duration_ms;
    g_buzzer_active = true;
}

void buzzer_update(uint32_t now_ms) {
    if (g_buzzer_active && (int32_t)(now_ms - g_buzzer_stop_ms) >= 0) {
        buzzer_stop();
    }
}
