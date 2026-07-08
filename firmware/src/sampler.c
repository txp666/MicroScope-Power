#include "sampler.h"

#include "ads8866.h"

#include "pico/critical_section.h"
#include "pico/multicore.h"
#include "pico/time.h"

#include <string.h>

#define ADS8866_CONVERSION_WAIT_US 9u
#define SAMPLE_PERIOD_Q_SHIFT 16u
#define AUTO_RANGE_RAIL_LOW_CODE 512u
#define AUTO_RANGE_RAIL_HIGH_CODE 60000u
#define AUTO_RANGE_10_TO_1_DELTA_CODE 30000u
#define AUTO_RANGE_1_TO_10_DELTA_CODE 1800u
#define AUTO_RANGE_10_TO_1_CONFIRM_FRAMES 1u
#define AUTO_RANGE_1_TO_10_CONFIRM_FRAMES 64u
#define AUTO_RANGE_SETTLE_FRAMES 6u

static frame_queue_t g_queue;
static critical_section_t g_queue_lock;

static volatile bool g_sampler_core_ready = false;
static volatile bool g_capture_active = false;
static volatile bool g_running = false;
static volatile uint32_t g_sample_rate = PROTOCOL_DEFAULT_SAMPLE_RATE;
static volatile uint16_t g_last_sample = 0;
static volatile uint16_t g_zero_code_1_ohm = PROTOCOL_DEFAULT_ZERO_CODE;
static volatile uint16_t g_zero_code_10_ohm = PROTOCOL_DEFAULT_ZERO_CODE;
static volatile bool g_auto_range = true;

static wave_frame_t *g_current_frame = NULL;
static uint16_t g_current_count = 0;
static uint16_t g_current_min = UINT16_MAX;
static uint16_t g_current_max = 0;
static bool g_current_discard = false;
static volatile uint16_t g_next_seq = 0;
static uint8_t g_range_up_count = 0;
static uint8_t g_range_down_count = 0;
static uint8_t g_range_settle_frames = 0;

static uint16_t code_distance(uint16_t a, uint16_t b) {
    return a > b ? (uint16_t)(a - b) : (uint16_t)(b - a);
}

static uint16_t zero_code_for_range(ads8866_range_t range) {
    return range == ADS8866_RANGE_10_OHM ? g_zero_code_10_ohm : g_zero_code_1_ohm;
}

static uint16_t frame_flags(void) {
    uint16_t flags = PROTOCOL_FLAG_ADC_DATA;
    if (ads8866_get_range() == ADS8866_RANGE_10_OHM) {
        flags |= PROTOCOL_FLAG_RANGE_10_OHM;
    }
    if (g_auto_range) {
        flags |= PROTOCOL_FLAG_AUTO_RANGE;
    }
    return flags;
}

static void reset_auto_range_state(void) {
    g_range_up_count = 0;
    g_range_down_count = 0;
    g_range_settle_frames = 0;
}

static void switch_range_for_auto(ads8866_range_t range) {
    if (ads8866_get_range() == range) {
        return;
    }

    ads8866_set_range(range);
    g_range_up_count = 0;
    g_range_down_count = 0;
    g_range_settle_frames = AUTO_RANGE_SETTLE_FRAMES;
}

static void update_auto_range(uint16_t min_code, uint16_t max_code) {
    if (!g_auto_range) {
        reset_auto_range_state();
        return;
    }

    const ads8866_range_t range = ads8866_get_range();
    const uint16_t zero = zero_code_for_range(range);
    const uint16_t max_delta = code_distance(max_code, zero) > code_distance(min_code, zero)
                                   ? code_distance(max_code, zero)
                                   : code_distance(min_code, zero);

    if (range == ADS8866_RANGE_10_OHM) {
        if (max_code >= AUTO_RANGE_RAIL_HIGH_CODE ||
            min_code <= AUTO_RANGE_RAIL_LOW_CODE ||
            max_delta >= AUTO_RANGE_10_TO_1_DELTA_CODE) {
            if (g_range_down_count < AUTO_RANGE_10_TO_1_CONFIRM_FRAMES) {
                g_range_down_count++;
            }
            if (g_range_down_count >= AUTO_RANGE_10_TO_1_CONFIRM_FRAMES) {
                switch_range_for_auto(ADS8866_RANGE_1_OHM);
            }
        } else {
            g_range_down_count = 0;
        }
        return;
    }

    if (max_delta <= AUTO_RANGE_1_TO_10_DELTA_CODE &&
        max_code < AUTO_RANGE_RAIL_HIGH_CODE &&
        min_code > AUTO_RANGE_RAIL_LOW_CODE) {
        if (g_range_up_count < AUTO_RANGE_1_TO_10_CONFIRM_FRAMES) {
            g_range_up_count++;
        }
        if (g_range_up_count >= AUTO_RANGE_1_TO_10_CONFIRM_FRAMES) {
            switch_range_for_auto(ADS8866_RANGE_10_OHM);
        }
    } else {
        g_range_up_count = 0;
    }
}

static void queue_reset_locked(void) {
    memset(&g_queue, 0, sizeof(g_queue));
}

static wave_frame_t *queue_reserve_frame(uint16_t *flags) {
    critical_section_enter_blocking(&g_queue_lock);

    if (g_queue.count >= FRAME_QUEUE_DEPTH) {
        g_queue.read_index = (uint8_t)((g_queue.read_index + 1u) % FRAME_QUEUE_DEPTH);
        g_queue.count--;
        g_queue.dropped++;
        *flags |= PROTOCOL_FLAG_OVERFLOW;
    }

    wave_frame_t *frame = &g_queue.frames[g_queue.write_index];

    critical_section_exit(&g_queue_lock);
    return frame;
}

static void queue_publish_reserved_frame(void) {
    critical_section_enter_blocking(&g_queue_lock);
    g_queue.write_index = (uint8_t)((g_queue.write_index + 1u) % FRAME_QUEUE_DEPTH);
    g_queue.count++;
    critical_section_exit(&g_queue_lock);
}

static void append_sample(uint16_t sample, uint32_t t0_us, uint32_t sample_rate) {
    if (g_current_count == 0) {
        g_current_discard = g_range_settle_frames > 0u;
        if (!g_current_discard) {
            uint16_t flags = frame_flags();
            g_current_frame = queue_reserve_frame(&flags);
            protocol_prepare_frame(g_current_frame,
                                   g_next_seq++,
                                   t0_us,
                                   sample_rate,
                                   flags);
        }
        g_current_min = sample;
        g_current_max = sample;
    }

    g_last_sample = sample;
    if (sample < g_current_min) {
        g_current_min = sample;
    }
    if (sample > g_current_max) {
        g_current_max = sample;
    }
    if (!g_current_discard) {
        g_current_frame->samples[g_current_count] = sample;
    }
    g_current_count++;

    if (g_current_count >= SAMPLE_BLOCK_SIZE) {
        const uint16_t frame_min = g_current_min;
        const uint16_t frame_max = g_current_max;
        if (g_current_discard) {
            g_range_settle_frames--;
        } else {
            protocol_finish_frame(g_current_frame);
            queue_publish_reserved_frame();
            update_auto_range(frame_min, frame_max);
        }
        g_current_frame = NULL;
        g_current_count = 0;
        g_current_min = UINT16_MAX;
        g_current_max = 0;
        g_current_discard = false;
    }
}

static void sampler_core_capture_loop(void) {
    uint32_t sample_rate = g_sample_rate;
    uint64_t period_q = ((uint64_t)1000000u << SAMPLE_PERIOD_Q_SHIFT) / sample_rate;
    uint64_t next_start_q = (uint64_t)time_us_32() << SAMPLE_PERIOD_Q_SHIFT;
    uint32_t period_floor_us = (uint32_t)(period_q >> SAMPLE_PERIOD_Q_SHIFT);
    if (period_floor_us == 0u) {
        period_floor_us = 1u;
    }

    g_capture_active = true;

    while (g_running) {
        if (sample_rate != g_sample_rate) {
            sample_rate = g_sample_rate;
            period_q = ((uint64_t)1000000u << SAMPLE_PERIOD_Q_SHIFT) / sample_rate;
            next_start_q = (uint64_t)time_us_32() << SAMPLE_PERIOD_Q_SHIFT;
            period_floor_us = (uint32_t)(period_q >> SAMPLE_PERIOD_Q_SHIFT);
            if (period_floor_us == 0u) {
                period_floor_us = 1u;
            }
        }

        const uint32_t next_start_us = (uint32_t)(next_start_q >> SAMPLE_PERIOD_Q_SHIFT);
        while ((int32_t)(time_us_32() - next_start_us) < 0) {
            tight_loop_contents();
        }

        ads8866_start_conversion();
        const uint32_t sample_start_us = time_us_32();
        busy_wait_us_32(ADS8866_CONVERSION_WAIT_US);
        append_sample(ads8866_finish_conversion(), sample_start_us, sample_rate);
        next_start_q += period_q;

        const uint32_t next_deadline_us = (uint32_t)(next_start_q >> SAMPLE_PERIOD_Q_SHIFT);
        if ((int32_t)(time_us_32() - next_deadline_us) >= (int32_t)period_floor_us) {
            next_start_q = (uint64_t)(time_us_32() + period_floor_us) << SAMPLE_PERIOD_Q_SHIFT;
        }
    }

    g_current_frame = NULL;
    g_current_count = 0;
    g_current_min = UINT16_MAX;
    g_current_max = 0;
    g_current_discard = false;
    g_capture_active = false;
}

static void sampler_core_entry(void) {
    multicore_lockout_victim_init();
    g_sampler_core_ready = true;

    while (true) {
        if (g_running) {
            sampler_core_capture_loop();
        }

        tight_loop_contents();
    }
}

void sampler_init(void) {
    critical_section_init(&g_queue_lock);

    critical_section_enter_blocking(&g_queue_lock);
    queue_reset_locked();
    critical_section_exit(&g_queue_lock);

    ads8866_set_sample_rate(g_sample_rate);

    multicore_launch_core1(sampler_core_entry);
    while (!g_sampler_core_ready) {
        tight_loop_contents();
    }
}

bool sampler_set_rate(uint32_t sample_rate) {
    if (!protocol_sample_rate_supported(sample_rate)) {
        return false;
    }

    bool restart = sampler_is_running();
    if (restart) {
        sampler_stop();
    }

    g_sample_rate = sample_rate;
    ads8866_set_sample_rate(g_sample_rate);

    if (restart) {
        return sampler_start();
    }

    return true;
}

uint32_t sampler_get_rate(void) {
    return g_sample_rate;
}

bool sampler_start(void) {
    if (g_running) {
        return true;
    }

    while (g_capture_active) {
        tight_loop_contents();
    }

    critical_section_enter_blocking(&g_queue_lock);
    queue_reset_locked();
    critical_section_exit(&g_queue_lock);

    g_current_count = 0;
    g_current_frame = NULL;
    g_current_min = UINT16_MAX;
    g_current_max = 0;
    g_current_discard = false;
    g_next_seq = 0;
    reset_auto_range_state();
    if (g_auto_range) {
        ads8866_set_range(ADS8866_RANGE_1_OHM);
        g_range_settle_frames = AUTO_RANGE_SETTLE_FRAMES;
    }
    g_running = true;

    return true;
}

void sampler_stop(void) {
    g_running = false;

    while (g_capture_active) {
        tight_loop_contents();
    }
}

bool sampler_is_running(void) {
    return g_running;
}

bool sampler_pop_frame(wave_frame_t *out) {
    bool ok = false;

    critical_section_enter_blocking(&g_queue_lock);
    if (g_queue.count > 0) {
        *out = g_queue.frames[g_queue.read_index];
        g_queue.read_index = (uint8_t)((g_queue.read_index + 1u) % FRAME_QUEUE_DEPTH);
        g_queue.count--;
        ok = true;
    }
    critical_section_exit(&g_queue_lock);

    return ok;
}

uint32_t sampler_get_dropped(void) {
    critical_section_enter_blocking(&g_queue_lock);
    uint32_t dropped = g_queue.dropped;
    critical_section_exit(&g_queue_lock);
    return dropped;
}

uint16_t sampler_get_next_seq(void) {
    return g_next_seq;
}

uint16_t sampler_get_last_sample(void) {
    return g_last_sample;
}

void sampler_set_zero_code(uint16_t zero_code) {
    sampler_set_zero_code_for_range(ads8866_get_range(), zero_code);
}

uint16_t sampler_get_zero_code(void) {
    return sampler_get_zero_code_for_range(ads8866_get_range());
}

void sampler_set_zero_code_for_range(ads8866_range_t range, uint16_t zero_code) {
    if (range == ADS8866_RANGE_10_OHM) {
        g_zero_code_10_ohm = zero_code;
    } else {
        g_zero_code_1_ohm = zero_code;
    }
}

uint16_t sampler_get_zero_code_for_range(ads8866_range_t range) {
    return zero_code_for_range(range);
}

bool sampler_set_range(ads8866_range_t range) {
    bool restart = sampler_is_running();
    if (restart) {
        sampler_stop();
    }

    ads8866_set_range(range);
    reset_auto_range_state();

    if (restart) {
        return sampler_start();
    }

    return true;
}

ads8866_range_t sampler_get_range(void) {
    return ads8866_get_range();
}

void sampler_set_auto_range(bool enabled) {
    g_auto_range = enabled;
    reset_auto_range_state();
    if (enabled) {
        (void)sampler_set_range(ADS8866_RANGE_1_OHM);
        g_range_settle_frames = AUTO_RANGE_SETTLE_FRAMES;
    }
}

bool sampler_get_auto_range(void) {
    return g_auto_range;
}
