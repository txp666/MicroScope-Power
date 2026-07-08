#pragma once

#include <stdint.h>

void adc_voltage_init(void);
uint16_t adc_voltage_read_raw(void);
uint32_t adc_voltage_raw_to_mv(uint16_t raw);
uint32_t adc_voltage_pin_mv_to_input_mv(uint32_t pin_mv);
uint32_t adc_voltage_read_pin_mv(uint16_t *raw_out);
uint32_t adc_voltage_read_input_mv(uint16_t *raw_out, uint32_t *pin_mv_out);
