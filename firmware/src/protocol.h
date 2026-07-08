#pragma once

#include <stdbool.h>
#include <stdint.h>

#define PROTOCOL_MAGIC 0xA55Au
#define PROTOCOL_FRAME_SAMPLE_COUNT 256u
#define PROTOCOL_WAVE_FRAME_BYTES 530u

#define PROTOCOL_FLAG_ADC_DATA (1u << 0)
#define PROTOCOL_FLAG_OVERFLOW (1u << 1)
#define PROTOCOL_FLAG_RANGE_10_OHM (1u << 2)
#define PROTOCOL_FLAG_AUTO_RANGE (1u << 3)

#define PROTOCOL_SRATE_10K 10000u
#define PROTOCOL_SRATE_20K 20000u
#define PROTOCOL_SRATE_50K 50000u
#define PROTOCOL_SRATE_100K 100000u
#define PROTOCOL_DEFAULT_SAMPLE_RATE PROTOCOL_SRATE_100K

#define PROTOCOL_DEFAULT_ZERO_CODE 0u

#define PROTOCOL_CMD_MAX_LEN 64u

typedef struct __attribute__((packed)) {
    uint16_t magic;
    uint16_t seq;
    uint32_t t0_us;
    uint32_t sample_rate;
    uint16_t count;
    uint16_t flags;
    uint16_t samples[PROTOCOL_FRAME_SAMPLE_COUNT];
    uint16_t crc;
} wave_frame_t;

_Static_assert(sizeof(wave_frame_t) == PROTOCOL_WAVE_FRAME_BYTES, "wave_frame_t size must stay 530 bytes");

bool protocol_sample_rate_supported(uint32_t sample_rate);
void protocol_prepare_frame(wave_frame_t *frame, uint16_t seq, uint32_t t0_us, uint32_t sample_rate, uint16_t flags);
void protocol_finish_frame(wave_frame_t *frame);
