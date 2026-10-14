#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ads8866.h"
#include "protocol.h"

#define SAMPLE_BLOCK_SIZE PROTOCOL_FRAME_SAMPLE_COUNT
#define FRAME_QUEUE_DEPTH 8u

typedef struct {
    wave_frame_t frames[FRAME_QUEUE_DEPTH];
    volatile uint8_t write_index;
    volatile uint8_t read_index;
    volatile uint8_t count;
    volatile uint32_t dropped;
} frame_queue_t;

void sampler_init(void);
bool sampler_set_rate(uint32_t sample_rate);
uint32_t sampler_get_rate(void);
bool sampler_start(void);
void sampler_stop(void);
bool sampler_is_running(void);

bool sampler_pop_frame(wave_frame_t *out);
uint32_t sampler_get_dropped(void);
uint16_t sampler_get_next_seq(void);
uint16_t sampler_get_last_sample(void);
/* Cumulative missed scheduling deadlines; not a count of discarded frames. */
uint32_t sampler_get_timing_overruns(void);

void sampler_set_zero_code(uint16_t zero_code);
uint16_t sampler_get_zero_code(void);
void sampler_set_zero_code_for_range(ads8866_range_t range, uint16_t zero_code);
uint16_t sampler_get_zero_code_for_range(ads8866_range_t range);

bool sampler_set_range(ads8866_range_t range);
ads8866_range_t sampler_get_range(void);
void sampler_set_auto_range(bool enabled);
bool sampler_get_auto_range(void);
