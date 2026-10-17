#include "sampler.h"

#include "ads8866.h"

#include "pico/critical_section.h"
#include "pico/multicore.h"
#include "pico/time.h"

#include <string.h>

#define ADS8866_CONVERSION_WAIT_US 9u
#define SAMPLE_PERIOD_Q_SHIFT 16u
#define AUTO_RANGE_10_TO_1_DELTA_CODE 30000u
#define AUTO_RANGE_1_TO_10_DELTA_CODE 1800u
#define AUTO_RANGE_10_TO_1_CONFIRM_FRAMES 1u
#define AUTO_RANGE_1_TO_10_CONFIRM_FRAMES 64u
#define AUTO_RANGE_SETTLE_FRAMES 6u
#define LOCAL_BUCKET_US 50000u
#define LOCAL_BUCKET_COUNT 40u
#define LOCAL_MAX_GAP_US 5000u
#define LOCAL_RANGE_BATCH_SAMPLES 256u
#define LOCAL_SETTLE_SAMPLES (AUTO_RANGE_SETTLE_FRAMES * SAMPLE_BLOCK_SIZE)

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
static uint16_t g_pending_flags = 0;
static bool g_timing_overrun_pending = false;
static volatile uint32_t g_timing_overruns = 0;

typedef struct {
    uint64_t code_sum;
    uint32_t sample_count;
} local_bucket_t;

static local_bucket_t g_local_buckets[LOCAL_BUCKET_COUNT];
static uint64_t g_local_code_sum = 0;
static uint32_t g_local_sample_count = 0;
static uint8_t g_local_bucket_index = 0;
static uint8_t g_local_bucket_count = 0;
static local_bucket_t g_local_partial;
static uint32_t g_local_bucket_start_us = 0;
static uint32_t g_local_last_bucket_us = 0;
static uint32_t g_local_last_sample_us = 0;
static bool g_local_time_valid = false;
static bool g_local_metadata_valid = false;
static ads8866_range_t g_local_range = ADS8866_RANGE_1_OHM;
static uint16_t g_local_zero = 0;
static uint16_t g_local_batch_count = 0;
static uint16_t g_local_batch_min = UINT16_MAX;
static uint16_t g_local_batch_max = 0;
static uint32_t g_local_settle_samples = 0;

/* Caller holds g_queue_lock, which also owns idle ADS8866 transactions. */
static void local_current_reset_locked(void) {
    memset(g_local_buckets, 0, sizeof(g_local_buckets));
    g_local_code_sum = 0;
    g_local_sample_count = 0;
    g_local_bucket_index = 0;
    g_local_bucket_count = 0;
    memset(&g_local_partial, 0, sizeof(g_local_partial));
    g_local_time_valid = false;
    g_local_metadata_valid = false;
    g_local_batch_count = 0;
    g_local_batch_min = UINT16_MAX;
    g_local_batch_max = 0;
    g_local_settle_samples = 0;
}

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
        if (ads8866_codes_at_rail(min_code, max_code, zero) ||
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
        !ads8866_codes_at_rail(min_code, max_code, zero)) {
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

/* Close only an already sampled bucket; never fabricate missing samples. */
static void local_current_roll_bucket_locked(uint32_t now_us) {
    if (g_local_metadata_valid && g_local_partial.sample_count > 0u &&
        (uint32_t)(now_us - g_local_bucket_start_us) >= LOCAL_BUCKET_US) {
        local_bucket_t *old = &g_local_buckets[g_local_bucket_index];
        g_local_code_sum -= old->code_sum;
        g_local_sample_count -= old->sample_count;
        *old = g_local_partial;
        g_local_code_sum += old->code_sum;
        g_local_sample_count += old->sample_count;
        g_local_bucket_index = (uint8_t)((g_local_bucket_index + 1u) % LOCAL_BUCKET_COUNT);
        if (g_local_bucket_count < LOCAL_BUCKET_COUNT) {
            ++g_local_bucket_count;
        }
        g_local_bucket_start_us += LOCAL_BUCKET_US;
        g_local_last_bucket_us = g_local_bucket_start_us;
        memset(&g_local_partial, 0, sizeof(g_local_partial));
    }
}

static void local_current_append_locked(uint16_t sample, uint32_t now_us,
                                        ads8866_range_t range, uint16_t zero) {
    if (!g_local_metadata_valid) {
        g_local_range = range;
        g_local_zero = zero;
        g_local_bucket_start_us = now_us;
        g_local_metadata_valid = true;
    }
    local_current_roll_bucket_locked(now_us);

    g_local_partial.code_sum += sample;
    ++g_local_partial.sample_count;
}

static void sampler_core_local_tick_locked(void) {
    uint32_t now_us = time_us_32();
    if (g_local_time_valid &&
        (uint32_t)(now_us - g_local_last_sample_us) > LOCAL_MAX_GAP_US) {
        uint32_t settling = g_local_settle_samples;
        local_current_reset_locked();
        g_local_settle_samples = settling;
    }

    ads8866_range_t range = ads8866_get_range();
    uint16_t zero = zero_code_for_range(range);
    if (g_local_metadata_valid && (range != g_local_range || zero != g_local_zero)) {
        local_current_reset_locked();
        g_local_settle_samples = LOCAL_SETTLE_SAMPLES;
    }

    ads8866_start_conversion();
    busy_wait_us_32(ADS8866_CONVERSION_WAIT_US);
    uint16_t sample;
    if (!ads8866_try_finish_conversion(&sample)) {
        uint32_t settling = g_local_settle_samples;
        local_current_reset_locked();
        g_local_settle_samples = settling;
        /* Each loop performs one real conversion, never a catch-up burst. */
        g_local_last_sample_us = time_us_32();
        g_local_time_valid = true;
        return;
    }
    g_local_last_sample_us = time_us_32();
    g_local_time_valid = true;

    if (g_local_settle_samples > 0u) {
        --g_local_settle_samples;
        return;
    }

    if (sample < g_local_batch_min) { g_local_batch_min = sample; }
    if (sample > g_local_batch_max) { g_local_batch_max = sample; }
    if (++g_local_batch_count >= LOCAL_RANGE_BATCH_SAMPLES) {
        update_auto_range(g_local_batch_min, g_local_batch_max);
        g_local_batch_count = 0;
        g_local_batch_min = UINT16_MAX;
        g_local_batch_max = 0;
        if (ads8866_get_range() != range) {
            local_current_reset_locked();
            g_local_settle_samples = LOCAL_SETTLE_SAMPLES;
            /* Idle settling does not consume or publish stream frames. */
            g_range_settle_frames = 0;
            return;
        }
    }
    local_current_append_locked(sample, now_us, range, zero);
}

static bool local_current_snapshot_locked(sampler_local_current_t *out) {
    uint32_t now_us = time_us_32();
    bool metadata_matches = g_local_metadata_valid &&
                            g_local_range == ads8866_get_range() &&
                            g_local_zero == zero_code_for_range(g_local_range);
    if (!g_running && !g_capture_active && metadata_matches && g_local_time_valid &&
        (uint32_t)(now_us - g_local_last_sample_us) <= LOCAL_MAX_GAP_US) {
        local_current_roll_bucket_locked(now_us);
    }
    bool valid = !g_running && !g_capture_active &&
                 metadata_matches && g_local_bucket_count == LOCAL_BUCKET_COUNT &&
                 g_local_sample_count > 0u &&
                 (uint32_t)(now_us - g_local_last_sample_us) <= LOCAL_MAX_GAP_US &&
                 (uint32_t)(now_us - g_local_last_bucket_us) <= LOCAL_BUCKET_US + LOCAL_MAX_GAP_US;
    if (valid) {
        *out = (sampler_local_current_t){
            .code_sum = g_local_code_sum,
            .sample_count = g_local_sample_count,
            .age_us = (uint32_t)(now_us - g_local_last_bucket_us),
            .range = g_local_range,
            .zero_code = g_local_zero,
        };
    }
    return valid;
}

bool sampler_get_local_current(sampler_local_current_t *out) {
    if (!out) { return false; }
    critical_section_enter_blocking(&g_queue_lock);
    bool valid = local_current_snapshot_locked(out);
    critical_section_exit(&g_queue_lock);
    return valid;
}

bool sampler_zero_local_current(void) {
    critical_section_enter_blocking(&g_queue_lock);
    sampler_local_current_t snapshot;
    bool valid = local_current_snapshot_locked(&snapshot);
    if (valid) {
        uint16_t zero = (uint16_t)((snapshot.code_sum + snapshot.sample_count / 2u) /
                                   snapshot.sample_count);
        if (snapshot.range == ADS8866_RANGE_10_OHM) {
            g_zero_code_10_ohm = zero;
        } else {
            g_zero_code_1_ohm = zero;
        }
        reset_auto_range_state();
        local_current_reset_locked();
    }
    critical_section_exit(&g_queue_lock);
    return valid;
}

void sampler_reset_local_current(void) {
    critical_section_enter_blocking(&g_queue_lock);
    local_current_reset_locked();
    critical_section_exit(&g_queue_lock);
}

static void queue_reset_locked(void) {
    memset(&g_queue, 0, sizeof(g_queue));
}

static wave_frame_t *queue_reserve_frame(uint16_t *flags) {
    critical_section_enter_blocking(&g_queue_lock);

    if (g_queue.count >= FRAME_QUEUE_DEPTH) {
        const uint16_t carried_flags = g_queue.frames[g_queue.read_index].flags &
                                      (PROTOCOL_FLAG_STREAM_START | PROTOCOL_FLAG_GAP_BEFORE);
        g_queue.read_index = (uint8_t)((g_queue.read_index + 1u) % FRAME_QUEUE_DEPTH);
        g_queue.count--;
        g_queue.dropped++;
        /* Preserve a session boundary even if its first frame is evicted. */
        g_queue.frames[g_queue.read_index].flags |= carried_flags;
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

static void reset_current_frame(void) {
    g_current_frame = NULL;
    g_current_count = 0;
    g_current_min = UINT16_MAX;
    g_current_max = 0;
    g_current_discard = false;
}

static void discard_current_frame(void) {
    reset_current_frame();
    g_pending_flags |= PROTOCOL_FLAG_GAP_BEFORE;
}

static void append_sample(uint16_t sample, uint32_t t0_us, uint32_t sample_rate) {
    if (g_current_count == 0) {
        if (g_timing_overrun_pending) {
            g_pending_flags |= PROTOCOL_FLAG_TIMING_OVERRUN;
            g_timing_overrun_pending = false;
        }
        g_current_discard = g_range_settle_frames > 0u;
        if (!g_current_discard) {
            uint16_t flags = frame_flags() | g_pending_flags;
            g_current_frame = queue_reserve_frame(&flags);
            protocol_prepare_frame(g_current_frame,
                                   g_next_seq,
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
            if (g_range_settle_frames > 0u) {
                g_range_settle_frames--;
            }
            g_pending_flags |= PROTOCOL_FLAG_GAP_BEFORE;
        } else {
            protocol_finish_frame(g_current_frame);
            queue_publish_reserved_frame();
            g_next_seq++;
            g_pending_flags = 0;
            update_auto_range(frame_min, frame_max);
        }
        reset_current_frame();
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

    while (g_running) {
        const uint32_t next_start_us = (uint32_t)(next_start_q >> SAMPLE_PERIOD_Q_SHIFT);
        while ((int32_t)(time_us_32() - next_start_us) < 0) {
            tight_loop_contents();
        }

        ads8866_start_conversion();
        const uint32_t sample_start_us = time_us_32();
        busy_wait_us_32(ADS8866_CONVERSION_WAIT_US);
        uint16_t sample;
        if (ads8866_try_finish_conversion(&sample)) {
            append_sample(sample, sample_start_us, sample_rate);
        } else {
            discard_current_frame();
        }
        next_start_q += period_q;

        const uint32_t next_deadline_us = (uint32_t)(next_start_q >> SAMPLE_PERIOD_Q_SHIFT);
        if ((int32_t)(time_us_32() - next_deadline_us) >= (int32_t)period_floor_us) {
            /* Every successful ADC result remains in the stream. Report slower
             * scheduling separately from missing samples; it is not a gap. */
            g_timing_overruns++;
            g_timing_overrun_pending = true;
            next_start_q = (uint64_t)(time_us_32() + period_floor_us) << SAMPLE_PERIOD_Q_SHIFT;
        }
    }

    reset_current_frame();
    critical_section_enter_blocking(&g_queue_lock);
    g_capture_active = false;
    critical_section_exit(&g_queue_lock);
}

static void sampler_core_entry(void) {
    multicore_lockout_victim_init();
    g_sampler_core_ready = true;

    while (true) {
        /* Claim capture under the same lock that acknowledges STOP. Otherwise
         * STOP can miss a core that has observed RUN but not yet set ACTIVE. */
        critical_section_enter_blocking(&g_queue_lock);
        const bool capture = g_running;
        if (capture) {
            g_capture_active = true;
        } else {
            sampler_core_local_tick_locked();
        }
        critical_section_exit(&g_queue_lock);
        if (capture) {
            sampler_core_capture_loop();
        }

        tight_loop_contents();
    }
}

void sampler_init(void) {
    critical_section_init(&g_queue_lock);

    critical_section_enter_blocking(&g_queue_lock);
    queue_reset_locked();
    local_current_reset_locked();
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
    if (sample_rate == g_sample_rate) {
        return true;
    }

    bool restart = sampler_is_running();
    if (restart) {
        sampler_stop();
    }

    critical_section_enter_blocking(&g_queue_lock);
    g_sample_rate = sample_rate;
    ads8866_set_sample_rate(g_sample_rate);
    local_current_reset_locked();
    critical_section_exit(&g_queue_lock);

    if (restart) {
        return sampler_start();
    }

    return true;
}

uint32_t sampler_get_rate(void) {
    return g_sample_rate;
}

bool sampler_start(void) {
    critical_section_enter_blocking(&g_queue_lock);
    if (g_running) {
        critical_section_exit(&g_queue_lock);
        return true;
    }
    /* Every transition to stopped waits for ACTIVE to clear before returning. */
    queue_reset_locked();
    local_current_reset_locked();
    reset_current_frame();
    g_next_seq = 0;
    g_pending_flags = PROTOCOL_FLAG_STREAM_START;
    g_timing_overrun_pending = false;
    reset_auto_range_state();
    if (g_auto_range) {
        ads8866_set_range(ADS8866_RANGE_1_OHM);
        g_range_settle_frames = AUTO_RANGE_SETTLE_FRAMES;
    }
    g_running = true;
    critical_section_exit(&g_queue_lock);

    return true;
}

void sampler_stop(void) {
    critical_section_enter_blocking(&g_queue_lock);
    g_running = false;
    local_current_reset_locked();
    bool active = g_capture_active;
    critical_section_exit(&g_queue_lock);
    while (active) {
        tight_loop_contents();
        critical_section_enter_blocking(&g_queue_lock);
        active = g_capture_active;
        critical_section_exit(&g_queue_lock);
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

uint32_t sampler_get_timing_overruns(void) {
    return g_timing_overruns;
}

void sampler_set_zero_code(uint16_t zero_code) {
    critical_section_enter_blocking(&g_queue_lock);
    ads8866_range_t range = ads8866_get_range();
    critical_section_exit(&g_queue_lock);
    sampler_set_zero_code_for_range(range, zero_code);
}

uint16_t sampler_get_zero_code(void) {
    return sampler_get_zero_code_for_range(sampler_get_range());
}

void sampler_set_zero_code_for_range(ads8866_range_t range, uint16_t zero_code) {
    if (zero_code_for_range(range) == zero_code) {
        return;
    }
    const bool restart = sampler_is_running();
    sampler_stop();
    critical_section_enter_blocking(&g_queue_lock);
    if (range == ADS8866_RANGE_10_OHM) {
        g_zero_code_10_ohm = zero_code;
    } else {
        g_zero_code_1_ohm = zero_code;
    }
    reset_auto_range_state();
    local_current_reset_locked();
    critical_section_exit(&g_queue_lock);
    if (restart) {
        (void)sampler_start();
    }
}

uint16_t sampler_get_zero_code_for_range(ads8866_range_t range) {
    return zero_code_for_range(range);
}

bool sampler_set_range(ads8866_range_t range) {
    critical_section_enter_blocking(&g_queue_lock);
    bool unchanged = ads8866_get_range() == range;
    critical_section_exit(&g_queue_lock);
    if (unchanged) {
        return true;
    }
    bool restart = sampler_is_running();
    if (restart) {
        sampler_stop();
    }

    critical_section_enter_blocking(&g_queue_lock);
    ads8866_set_range(range);
    reset_auto_range_state();
    local_current_reset_locked();
    g_local_settle_samples = LOCAL_SETTLE_SAMPLES;
    critical_section_exit(&g_queue_lock);

    if (restart) {
        return sampler_start();
    }

    return true;
}

ads8866_range_t sampler_get_range(void) {
    critical_section_enter_blocking(&g_queue_lock);
    ads8866_range_t range = ads8866_get_range();
    critical_section_exit(&g_queue_lock);
    return range;
}

void sampler_set_auto_range(bool enabled) {
    if (g_auto_range == enabled) {
        return;
    }
    const bool restart = sampler_is_running();
    sampler_stop();
    critical_section_enter_blocking(&g_queue_lock);
    g_auto_range = enabled;
    reset_auto_range_state();
    local_current_reset_locked();
    if (enabled) {
        ads8866_set_range(ADS8866_RANGE_1_OHM);
        g_range_settle_frames = AUTO_RANGE_SETTLE_FRAMES;
        g_local_settle_samples = LOCAL_SETTLE_SAMPLES;
    }
    critical_section_exit(&g_queue_lock);
    if (restart) {
        (void)sampler_start();
    }
}

bool sampler_get_auto_range(void) {
    return g_auto_range;
}
