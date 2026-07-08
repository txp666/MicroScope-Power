#include "ads8866.h"

#include "board.h"

#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"

#define ADS8866_CS_TO_MSB_WAIT_CYCLES 16u
#define ADS8866_SPI_RUN_BAUD_HZ 24000000u

static ads8866_range_t g_range = ADS8866_RANGE_10_OHM;

static void drain_spi_rx(void) {
    while (spi_is_readable(ADS8866_SPI_PORT)) {
        (void)spi_get_hw(ADS8866_SPI_PORT)->dr;
    }
}

static uint16_t read_spi16(void) {
    uint16_t value = 0u;
    drain_spi_rx();
    spi_read16_blocking(ADS8866_SPI_PORT, 0u, &value, 1);
    return value;
}

void ads8866_init(void) {
    spi_init(ADS8866_SPI_PORT, ADS8866_SPI_BAUD_HZ);
    spi_set_format(ADS8866_SPI_PORT, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(ADS8866_PIN_MISO, GPIO_FUNC_SPI);
    gpio_disable_pulls(ADS8866_PIN_MISO);
    gpio_set_function(ADS8866_PIN_SCLK, GPIO_FUNC_SPI);

    gpio_init(ADS8866_PIN_MOSI);
    gpio_set_dir(ADS8866_PIN_MOSI, GPIO_OUT);
    gpio_put(ADS8866_PIN_MOSI, 1);

    gpio_init(ADS8866_PIN_CS);
    gpio_set_dir(ADS8866_PIN_CS, GPIO_OUT);
    gpio_put(ADS8866_PIN_CS, 0);

    gpio_init(ADS8866_PIN_SEL);
    gpio_set_dir(ADS8866_PIN_SEL, GPIO_OUT);
    ads8866_set_range(g_range);
}

void ads8866_set_sample_rate(uint32_t sample_rate) {
    (void)sample_rate;
    spi_set_baudrate(ADS8866_SPI_PORT, ADS8866_SPI_RUN_BAUD_HZ);
}

void ads8866_start_conversion(void) {
    gpio_put(ADS8866_PIN_MOSI, 1);
    gpio_put(ADS8866_PIN_CS, 1);
}

uint16_t ads8866_finish_conversion(void) {
    gpio_put(ADS8866_PIN_CS, 0);
    busy_wait_at_least_cycles(ADS8866_CS_TO_MSB_WAIT_CYCLES);
    uint16_t value = read_spi16();
    gpio_put(ADS8866_PIN_MOSI, 1);
    return value;
}

uint16_t ads8866_read_sample(void) {
    ads8866_start_conversion();
    busy_wait_us_32(9);
    return ads8866_finish_conversion();
}

void ads8866_set_range(ads8866_range_t range) {
    g_range = range;
    gpio_put(ADS8866_PIN_SEL, range == ADS8866_RANGE_10_OHM ? 1 : 0);
}

ads8866_range_t ads8866_get_range(void) {
    return g_range;
}

bool ads8866_range_is_high_sensitivity(void) {
    return g_range == ADS8866_RANGE_10_OHM;
}
