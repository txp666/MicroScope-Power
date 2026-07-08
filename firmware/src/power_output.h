#pragma once

#include <stdbool.h>
#include <stdint.h>

#define POWER_OUTPUT_VDAC_MIN_MV 0u
#define POWER_OUTPUT_VDAC_MAX_MV 2500u
#define POWER_OUTPUT_VDAC_STEP_MV 50u

void power_output_init(void);
bool power_output_set_vdac_mv(uint16_t mv);
bool power_output_ready(void);
bool power_output_power_good(void);
uint8_t power_output_dac_address(void);
uint16_t power_output_vdac_mv(void);
