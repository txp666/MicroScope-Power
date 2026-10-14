#include <assert.h>
#include <stdio.h>
#include "../../firmware/src/adc_voltage.c"

static uint16_t mock_code;
static unsigned read_count;
static unsigned input;
static unsigned adc_pin;
static bool initialized;
void adc_init(void) { initialized = true; }
void adc_gpio_init(unsigned pin) { adc_pin = pin; }
void adc_select_input(unsigned selected) { input = selected; }
uint16_t adc_read(void) { read_count++; return mock_code; }

int main(void) {
    adc_voltage_init();
    assert(initialized && adc_pin == 26u);
    mock_code = 2048u;
    uint16_t raw = 0;
    uint32_t pin_mv = 0;
    assert(adc_voltage_read_input_mv(&raw, &pin_mv) == 3300u);
    assert(raw == 2048u && pin_mv == 1650u && input == 0u && read_count == 16u);
    mock_code = 4095u;
    assert(adc_voltage_read_input_mv(NULL, NULL) == 6600u && read_count == 32u);
    mock_code = 0u;
    assert(adc_voltage_read_input_mv(NULL, NULL) == 0u && read_count == 48u);
    puts("adc voltage: 16-read average, ADC0 selection, 100k/100k scaling and zero/full-scale passed");
    return 0;
}
