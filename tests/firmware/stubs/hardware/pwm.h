#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "pico/stdlib.h"

typedef unsigned int uint;
uint pwm_gpio_to_slice_num(uint gpio);
uint pwm_gpio_to_channel(uint gpio);
void pwm_set_enabled(uint slice, bool enabled);
void pwm_set_gpio_level(uint gpio, uint16_t level);
void pwm_set_counter(uint slice, uint16_t counter);
void pwm_set_clkdiv_int_frac(uint slice, uint8_t integer, uint8_t fractional);
void pwm_set_wrap(uint slice, uint16_t wrap);
void pwm_set_chan_level(uint slice, uint channel, uint16_t level);
