/* Compile the real sampler, exposing its frame boundaries for deterministic
 * fault injection; Pico synchronization is backed by real pthread mutexes. */
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <time.h>

#include "../../firmware/src/sampler.c"

static ads8866_range_t mock_range;
static atomic_uint mock_time;
static pthread_t core_thread;
static _Thread_local bool is_core;
static atomic_bool core_shutdown;
static atomic_bool pause_claim;
static atomic_bool claim_paused;
static atomic_bool release_claim;
static atomic_bool setter_done;
static atomic_uint conversion_count;
static unsigned conversion_attempt;
static unsigned fail_conversion_at;
static unsigned stop_after_conversion;

void critical_section_init(critical_section_t *section) {
    assert(pthread_mutex_init(&section->mutex, NULL) == 0);
}
void critical_section_enter_blocking(critical_section_t *section) {
    assert(pthread_mutex_lock(&section->mutex) == 0);
}
void critical_section_exit(critical_section_t *section) {
    bool pause = is_core && g_capture_active && atomic_exchange(&pause_claim, false);
    assert(pthread_mutex_unlock(&section->mutex) == 0);
    if (pause) {
        atomic_store(&claim_paused, true);
        while (!atomic_load(&release_claim)) { sched_yield(); }
    }
}
void tight_loop_contents(void) {
    if (is_core && atomic_load(&core_shutdown)) { pthread_exit(NULL); }
    sched_yield();
}
uint32_t time_us_32(void) { return atomic_fetch_add(&mock_time, 1); }
void busy_wait_us_32(uint32_t us) { atomic_fetch_add(&mock_time, us); }
void multicore_lockout_victim_init(void) {}
static void *run_core(void *entry) {
    (void)entry;
    is_core = true;
    sampler_core_entry();
    return NULL;
}
void multicore_launch_core1(void (*entry)(void)) {
    (void)entry;
    assert(pthread_create(&core_thread, NULL, run_core, NULL) == 0);
}
void ads8866_set_sample_rate(uint32_t rate) { (void)rate; }
void ads8866_set_range(ads8866_range_t range) { mock_range = range; }
ads8866_range_t ads8866_get_range(void) { return mock_range; }
void ads8866_start_conversion(void) { atomic_fetch_add(&conversion_count, 1); }
bool ads8866_try_finish_conversion(uint16_t *sample) {
    conversion_attempt++;
    if (conversion_attempt == stop_after_conversion) { g_running = false; }
    if (conversion_attempt == fail_conversion_at) { return false; }
    *sample = conversion_attempt < fail_conversion_at ? 111 : 222;
    return true;
}

static void reset_fixture(void) {
    g_running = false;
    g_capture_active = false;
    g_auto_range = false;
    g_zero_code_1_ohm = 0;
    g_zero_code_10_ohm = 0;
    g_next_seq = 0;
    g_pending_flags = 0;
    g_timing_overrun_pending = false;
    conversion_attempt = 0;
    fail_conversion_at = 0;
    stop_after_conversion = 0;
    mock_range = ADS8866_RANGE_1_OHM;
    queue_reset_locked();
    reset_current_frame();
    reset_auto_range_state();
}
static void emit_block(uint16_t value, uint32_t start) {
    for (unsigned i = 0; i < SAMPLE_BLOCK_SIZE; ++i) {
        append_sample(value, start + i * 100, PROTOCOL_SRATE_10K);
    }
}
static void test_zero_aware_autorange(void) {
    reset_fixture();
    g_auto_range = true;
    for (unsigned i = 0; i < AUTO_RANGE_1_TO_10_CONFIRM_FRAMES; ++i) {
        update_auto_range(0, 10);
    }
    assert(mock_range == ADS8866_RANGE_10_OHM);
    update_auto_range(0, 10);
    assert(mock_range == ADS8866_RANGE_10_OHM);
    g_zero_code_10_ohm = 32768;
    update_auto_range(0, 10);
    assert(mock_range == ADS8866_RANGE_1_OHM);
    assert(!ads8866_codes_at_rail(0, 100, 0));
    assert(ads8866_codes_at_rail(0, 100, 32768));
    assert(ads8866_codes_at_rail(1000, 65535, 0));
}
static void test_settle_and_gap(void) {
    reset_fixture();
    g_pending_flags = PROTOCOL_FLAG_STREAM_START;
    g_range_settle_frames = 2;
    emit_block(1, 0);
    emit_block(2, 25600);
    assert(g_queue.count == 0 && g_range_settle_frames == 0);
    emit_block(3, 51200);
    wave_frame_t frame;
    assert(sampler_pop_frame(&frame));
    assert(frame.seq == 0 && frame.t0_us == 51200);
    assert((frame.flags & (PROTOCOL_FLAG_STREAM_START | PROTOCOL_FLAG_GAP_BEFORE)) ==
           (PROTOCOL_FLAG_STREAM_START | PROTOCOL_FLAG_GAP_BEFORE));
    assert(frame.samples[255] == 3);

    /* A stopped/reset settle counter can never wrap to 255. */
    reset_fixture();
    g_range_settle_frames = 1;
    append_sample(1, 0, PROTOCOL_SRATE_10K);
    g_range_settle_frames = 0;
    for (unsigned i = 1; i < SAMPLE_BLOCK_SIZE; ++i) {
        append_sample(1, i * 100, PROTOCOL_SRATE_10K);
    }
    assert(g_range_settle_frames == 0);
}
static void test_partial_frame_discard(void) {
    reset_fixture();
    g_pending_flags = PROTOCOL_FLAG_STREAM_START;
    for (unsigned i = 0; i < 123; ++i) {
        append_sample(111, i * 100, PROTOCOL_SRATE_10K);
    }
    discard_current_frame();
    emit_block(222, 50000);
    wave_frame_t frame;
    assert(sampler_pop_frame(&frame));
    assert(frame.seq == 0 && frame.t0_us == 50000 && g_next_seq == 1);
    assert(frame.flags & PROTOCOL_FLAG_GAP_BEFORE);
    assert(frame.flags & PROTOCOL_FLAG_STREAM_START);
    for (unsigned i = 0; i < SAMPLE_BLOCK_SIZE; ++i) { assert(frame.samples[i] == 222); }
    assert(!sampler_pop_frame(&frame));
}
static void test_overflow_carries_session(void) {
    reset_fixture();
    g_pending_flags = PROTOCOL_FLAG_STREAM_START;
    for (unsigned i = 0; i < FRAME_QUEUE_DEPTH + 3; ++i) { emit_block(i, i * 25600); }
    assert(sampler_get_dropped() == 3);
    wave_frame_t frame;
    assert(sampler_pop_frame(&frame));
    assert(frame.seq == 3 && (frame.flags & PROTOCOL_FLAG_STREAM_START));
    assert(frame.magic == PROTOCOL_MAGIC && frame.count == 256 && sizeof(frame) == 530);
}
static void test_capture_spi_failure(void) {
    reset_fixture();
    g_sample_rate = PROTOCOL_SRATE_10K;
    g_running = true;
    g_capture_active = true;
    g_pending_flags = PROTOCOL_FLAG_STREAM_START;
    fail_conversion_at = 124;
    stop_after_conversion = 380;
    sampler_core_capture_loop();
    assert(!g_capture_active && !g_running);
    wave_frame_t frame;
    assert(sampler_pop_frame(&frame));
    assert(frame.seq == 0 && (frame.flags & PROTOCOL_FLAG_GAP_BEFORE));
    assert(frame.flags & PROTOCOL_FLAG_STREAM_START);
    for (unsigned i = 0; i < SAMPLE_BLOCK_SIZE; ++i) { assert(frame.samples[i] == 222); }
    assert(!sampler_pop_frame(&frame));
}
static void test_timing_overrun_preserves_continuous_capture(void) {
    reset_fixture();
    append_sample(7, 0, PROTOCOL_SRATE_10K);
    g_timing_overrun_pending = true;
    for (unsigned i = 1; i < SAMPLE_BLOCK_SIZE; ++i) {
        append_sample(7, i * 100, PROTOCOL_SRATE_10K);
    }
    emit_block(8, 30000);
    wave_frame_t frame;
    assert(sampler_pop_frame(&frame) && frame.seq == 0 && frame.samples[255] == 7);
    assert(!(frame.flags & PROTOCOL_FLAG_GAP_BEFORE));
    assert(sampler_pop_frame(&frame) && frame.seq == 1 && frame.samples[255] == 8);
    assert(!(frame.flags & PROTOCOL_FLAG_GAP_BEFORE));
    assert(frame.flags & PROTOCOL_FLAG_TIMING_OVERRUN);
}
static void test_unchanged_settings_preserve_frames(void) {
    reset_fixture();
    g_running = true;
    emit_block(3, 0);
    sampler_set_zero_code_for_range(ADS8866_RANGE_1_OHM, 0);
    sampler_set_auto_range(false);
    assert(sampler_set_range(ADS8866_RANGE_1_OHM));
    assert(sampler_set_rate(g_sample_rate));
    assert(sampler_start());
    assert(g_queue.count == 1 && g_next_seq == 1);
    sampler_stop();
}
static void wait_for(atomic_bool *condition) {
    struct timespec interval = {.tv_sec = 0, .tv_nsec = 1000000};
    for (unsigned i = 0; i < 2000 && !atomic_load(condition); ++i) { nanosleep(&interval, NULL); }
    assert(atomic_load(condition));
}
static void *disable_autorange(void *unused) {
    (void)unused;
    sampler_set_auto_range(false);
    atomic_store(&setter_done, true);
    return NULL;
}
static void test_stop_acknowledges_claimed_capture(void) {
    reset_fixture();
    g_auto_range = true;
    atomic_store(&pause_claim, true);
    multicore_launch_core1(sampler_core_entry);
    assert(sampler_start());
    wait_for(&claim_paused);
    pthread_t setter;
    assert(pthread_create(&setter, NULL, disable_autorange, NULL) == 0);
    struct timespec interval = {.tv_sec = 0, .tv_nsec = 20000000};
    nanosleep(&interval, NULL);
    assert(!atomic_load(&setter_done));
    assert(g_auto_range); /* Setter cannot reset capture-owned state before ACK. */
    atomic_store(&release_claim, true);
    wait_for(&setter_done);
    assert(pthread_join(setter, NULL) == 0);
    sampler_stop();
    assert(!g_capture_active && !sampler_is_running() && !g_auto_range);
    unsigned stopped_conversions = atomic_load(&conversion_count);
    nanosleep(&interval, NULL);
    assert(atomic_load(&conversion_count) == stopped_conversions);
    for (unsigned i = 0; i < 500; ++i) { assert(sampler_start()); sampler_stop(); }
    assert(!g_capture_active);
    atomic_store(&core_shutdown, true);
    assert(pthread_join(core_thread, NULL) == 0);
}
int main(void) {
    critical_section_init(&g_queue_lock);
    test_zero_aware_autorange();
    test_settle_and_gap();
    test_partial_frame_discard();
    test_overflow_carries_session();
    test_capture_spi_failure();
    test_timing_overrun_preserves_continuous_capture();
    test_unchanged_settings_preserve_frames();
    test_stop_acknowledges_claimed_capture();
    puts("sampler: zero-aware range, settle, invalid-frame gap, overflow/session and start/stop tests passed");
    return 0;
}
