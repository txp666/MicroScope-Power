#pragma once

#include <stdint.h>

void buzzer_init(void);
void buzzer_tone(uint32_t freq_hz, uint32_t duration_ms);
void buzzer_update(uint32_t now_ms);
