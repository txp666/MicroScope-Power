#include "adc_voltage.h"

#include "board.h"

#include "hardware/adc.h"

#define ADC0_AVERAGE_SAMPLES 16u
#define ADC_MAX_CODE 4095u

void adc_voltage_init(void) {
    adc_init();
    adc_gpio_init(ADC0_VOLTAGE_PIN);
}

uint16_t adc_voltage_read_raw(void) {
    adc_select_input(ADC0_VOLTAGE_INPUT);
    return adc_read();
}

uint32_t adc_voltage_raw_to_mv(uint16_t raw) {
    return ((uint32_t)raw * ADC0_VOLTAGE_REF_MV + (ADC_MAX_CODE / 2u)) / ADC_MAX_CODE;
}

uint32_t adc_voltage_pin_mv_to_input_mv(uint32_t pin_mv) {
    return (pin_mv * (ADC0_DIVIDER_TOP_OHM + ADC0_DIVIDER_BOTTOM_OHM) +
            (ADC0_DIVIDER_BOTTOM_OHM / 2u)) /
           ADC0_DIVIDER_BOTTOM_OHM;
}

uint32_t adc_voltage_read_pin_mv(uint16_t *raw_out) {
    uint32_t total = 0;

    adc_select_input(ADC0_VOLTAGE_INPUT);
    for (uint32_t i = 0; i < ADC0_AVERAGE_SAMPLES; ++i) {
        total += adc_read();
    }

    uint16_t raw = (uint16_t)((total + (ADC0_AVERAGE_SAMPLES / 2u)) / ADC0_AVERAGE_SAMPLES);
    if (raw_out) {
        *raw_out = raw;
    }

    return adc_voltage_raw_to_mv(raw);
}

uint32_t adc_voltage_read_input_mv(uint16_t *raw_out, uint32_t *pin_mv_out) {
    uint16_t raw = 0;
    uint32_t pin_mv = adc_voltage_read_pin_mv(&raw);
    if (raw_out) {
        *raw_out = raw;
    }
    if (pin_mv_out) {
        *pin_mv_out = pin_mv;
    }

    return adc_voltage_pin_mv_to_input_mv(pin_mv);
}
