#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    ADS8866_RANGE_1_OHM = 0,
    ADS8866_RANGE_10_OHM = 1,
} ads8866_range_t;

void ads8866_init(void);
void ads8866_set_sample_rate(uint32_t sample_rate);
void ads8866_start_conversion(void);
/* The try variants leave the output unchanged on a SPI timeout. */
bool ads8866_try_finish_conversion(uint16_t *sample_out);
bool ads8866_try_read_sample(uint16_t *sample_out);
uint32_t ads8866_get_read_errors(void);
void ads8866_set_range(ads8866_range_t range);
ads8866_range_t ads8866_get_range(void);
bool ads8866_range_is_high_sensitivity(void);
