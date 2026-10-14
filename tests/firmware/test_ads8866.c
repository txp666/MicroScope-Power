#include <assert.h>
#include <stdio.h>
#include "../../firmware/src/ads8866.c"

static spi_inst_t mock_spi;
spi_inst_t *spi0 = &mock_spi;
static unsigned readable_calls;
static uint64_t mock_now;
static bool fail_write;
static bool fail_read;
static bool mock_cs;
static uint16_t mock_sample;

bool spi_is_readable(spi_inst_t *spi) {
    if (readable_calls++ == 0 || fail_read) { return false; }
    spi->hw.dr = mock_sample;
    return true;
}
bool spi_is_writable(spi_inst_t *spi) { (void)spi; return !fail_write; }
spi_hw_t *spi_get_hw(spi_inst_t *spi) { return &spi->hw; }
uint32_t spi_init(spi_inst_t *spi, uint32_t baud) { (void)spi; return baud; }
void spi_set_format(spi_inst_t *spi, unsigned bits, unsigned cpol, unsigned cpha, unsigned order) {
    (void)spi; (void)bits; (void)cpol; (void)cpha; (void)order;
}
uint32_t spi_set_baudrate(spi_inst_t *spi, uint32_t baud) { (void)spi; return baud; }
void gpio_set_function(unsigned pin, unsigned function) { (void)pin; (void)function; }
void gpio_disable_pulls(unsigned pin) { (void)pin; }
void gpio_init(unsigned pin) { (void)pin; }
void gpio_set_dir(unsigned pin, bool out) { (void)pin; (void)out; }
void gpio_put(unsigned pin, bool value) { if (pin == ADS8866_PIN_CS) { mock_cs = value; } }
absolute_time_t make_timeout_time_us(uint32_t us) { return mock_now + us; }
bool time_reached(absolute_time_t deadline) { return mock_now++ >= deadline; }
void tight_loop_contents(void) {}
void busy_wait_us_32(uint32_t us) { mock_now += us; }
void busy_wait_at_least_cycles(uint32_t cycles) { (void)cycles; }

int main(void) {
    uint16_t sample = 999;
    mock_sample = 1234;
    assert(ads8866_try_read_sample(&sample));
    assert(sample == 1234 && ads8866_get_read_errors() == 0 && !mock_cs);
    readable_calls = 0;
    fail_write = true;
    sample = 999;
    assert(!ads8866_try_read_sample(&sample));
    assert(sample == 999 && ads8866_get_read_errors() == 1 && !mock_cs);
    readable_calls = 0;
    fail_write = false;
    fail_read = true;
    assert(!ads8866_try_read_sample(&sample));
    assert(sample == 999 && ads8866_get_read_errors() == 2 && !mock_cs);
    readable_calls = 0;
    fail_read = false;
    mock_sample = 4567;
    assert(ads8866_try_read_sample(&sample));
    assert(sample == 4567 && ads8866_get_read_errors() == 2);
    puts("ads8866: write/read timeout errors, untouched failed output and successful recovery passed");
    return 0;
}
