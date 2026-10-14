#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef uint64_t absolute_time_t;
uint32_t time_us_32(void);
void busy_wait_us_32(uint32_t us);
void tight_loop_contents(void);
absolute_time_t make_timeout_time_us(uint32_t us);
bool time_reached(absolute_time_t deadline);
void busy_wait_at_least_cycles(uint32_t cycles);
absolute_time_t get_absolute_time(void);
absolute_time_t make_timeout_time_ms(uint32_t ms);
uint32_t to_ms_since_boot(absolute_time_t time);
void sleep_ms(uint32_t ms);
