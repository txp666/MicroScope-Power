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
uint16_t ads8866_finish_conversion(void);
uint16_t ads8866_read_sample(void);
void ads8866_set_range(ads8866_range_t range);
ads8866_range_t ads8866_get_range(void);
bool ads8866_range_is_high_sensitivity(void);
