#pragma once

#include <stdbool.h>
#include <stdint.h>

void encoder_init(void);
void encoder_update(uint32_t now_ms);
int16_t encoder_get_delta(void);
bool encoder_get_click(void);
/* Consumes one 2 s hold event. Rotation cancels the current press gesture. */
bool encoder_get_long_press(void);
bool encoder_is_pressed(void);
