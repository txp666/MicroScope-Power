#pragma once
#include <stdbool.h>
#define GPIO_FUNC_SPI 1
#define GPIO_FUNC_PWM 4
#define GPIO_OUT 1
#define GPIO_OVERRIDE_NORMAL 0
#define GPIO_OVERRIDE_LOW 2
void gpio_set_function(unsigned pin, unsigned function);
void gpio_set_outover(unsigned pin, unsigned value);
void gpio_disable_pulls(unsigned pin);
void gpio_init(unsigned pin);
void gpio_set_dir(unsigned pin, bool out);
void gpio_put(unsigned pin, bool value);
