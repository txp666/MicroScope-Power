#include "protocol.h"

#include <string.h>

bool protocol_sample_rate_supported(uint32_t sample_rate) {
    return sample_rate == PROTOCOL_SRATE_10K ||
           sample_rate == PROTOCOL_SRATE_20K ||
           sample_rate == PROTOCOL_SRATE_50K ||
           sample_rate == PROTOCOL_SRATE_100K;
}

void protocol_prepare_frame(wave_frame_t *frame, uint16_t seq, uint32_t t0_us, uint32_t sample_rate, uint16_t flags) {
    memset(frame, 0, sizeof(*frame));
    frame->magic = PROTOCOL_MAGIC;
    frame->seq = seq;
    frame->t0_us = t0_us;
    frame->sample_rate = sample_rate;
    frame->count = PROTOCOL_FRAME_SAMPLE_COUNT;
    frame->flags = flags;
    frame->crc = 0;
}

void protocol_finish_frame(wave_frame_t *frame) {
    frame->magic = PROTOCOL_MAGIC;
    frame->count = PROTOCOL_FRAME_SAMPLE_COUNT;
    frame->crc = 0;
}
