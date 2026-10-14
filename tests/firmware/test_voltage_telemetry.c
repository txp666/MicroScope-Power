#include <assert.h>
#include <setjmp.h>
#include <stdio.h>

#define PICO_POWER_SCOPE_VERSION "0.2.2"
#define main firmware_main
#include "../../firmware/src/main.c"
#undef main

static uint64_t mock_time;
static uint32_t mock_vout_mv;
static unsigned voltage_reads;
static unsigned ui_updates;
static unsigned task_calls;
static bool mock_running;
static bool exercise_main_loop;
static jmp_buf loop_exit;
static char status_text[512];

uint32_t time_us_32(void) { return (uint32_t)mock_time; }
absolute_time_t get_absolute_time(void) { return mock_time; }
absolute_time_t make_timeout_time_ms(uint32_t ms) { return mock_time + ms * 1000u; }
uint32_t to_ms_since_boot(absolute_time_t time) { return (uint32_t)(time / 1000u); }
bool time_reached(absolute_time_t time) { return mock_time >= time; }
void sleep_ms(uint32_t ms) { mock_time += ms * 1000u; }
void tight_loop_contents(void) {}
void gpio_init(unsigned pin) { (void)pin; }
void gpio_set_dir(unsigned pin, bool out) { (void)pin; (void)out; }
void gpio_pull_up(unsigned pin) { (void)pin; }
bool gpio_get(unsigned pin) { (void)pin; return true; }
void reset_usb_boot(uint32_t gpio_activity_mask, uint32_t disable_interface_mask) {
    (void)gpio_activity_mask; (void)disable_interface_mask;
    assert(!"unexpected BOOTSEL");
}
uint32_t adc_voltage_read_input_mv(uint16_t *raw_out, uint32_t *pin_mv_out) {
    assert(raw_out == NULL && pin_mv_out == NULL);
    voltage_reads++;
    mock_time += 32u;
    return mock_vout_mv;
}
void adc_voltage_init(void) {}
void app_board_init(void) {}
void power_output_init(void) {}
void ads8866_init(void) {}
void sampler_init(void) {}
void local_ui_init(void) { mock_time += 3000000u; }
void local_ui_update(uint32_t now_ms) { (void)now_ms; ui_updates++; }
bool local_ui_take_output_save_request(void) { return false; }
void local_ui_set_sense_milliohm(ads8866_range_t range, uint32_t milliohm) {
    (void)range; (void)milliohm;
}
uint32_t local_ui_get_sense_milliohm(ads8866_range_t range) { (void)range; return UINT32_MAX; }
bool app_settings_load(app_settings_t *settings) { (void)settings; return false; }
bool app_settings_save(const app_settings_t *settings) { (void)settings; return true; }
uint16_t power_output_vdac_mv(void) { return 2500u; }
bool power_output_set_vdac_mv(uint16_t mv) { (void)mv; return true; }
uint32_t sampler_get_rate(void) { return 100000u; }
uint32_t sampler_get_dropped(void) { return UINT32_MAX; }
uint16_t sampler_get_next_seq(void) { return UINT16_MAX; }
uint16_t sampler_get_zero_code(void) { return UINT16_MAX; }
uint16_t sampler_get_zero_code_for_range(ads8866_range_t range) { (void)range; return UINT16_MAX; }
uint16_t sampler_get_last_sample(void) { return UINT16_MAX; }
ads8866_range_t sampler_get_range(void) { return ADS8866_RANGE_10_OHM; }
bool sampler_get_auto_range(void) { return true; }
uint32_t sampler_get_timing_overruns(void) { return UINT32_MAX; }
uint32_t ads8866_get_read_errors(void) { return UINT32_MAX; }
bool sampler_start(void) { mock_running = true; return true; }
void sampler_stop(void) { mock_running = false; }
bool sampler_is_running(void) { return mock_running; }
bool sampler_set_rate(uint32_t rate) { (void)rate; return true; }
void sampler_set_zero_code(uint16_t zero) { (void)zero; }
void sampler_set_zero_code_for_range(ads8866_range_t range, uint16_t zero) { (void)range; (void)zero; }
void sampler_set_auto_range(bool enabled) { (void)enabled; }
bool sampler_set_range(ads8866_range_t range) { (void)range; return true; }
bool sampler_pop_frame(wave_frame_t *out) { (void)out; return false; }
void usb_webusb_init(void) {}
bool usb_webusb_connected(void) { return true; }
bool usb_webusb_read_command(char *out, uint32_t out_size) { (void)out; (void)out_size; return false; }
bool usb_webusb_can_send_frame(void) { return false; }
bool usb_webusb_send_frame(const wave_frame_t *frame) { (void)frame; return false; }
bool usb_webusb_send_text(const char *text) {
    assert(strlen(text) < sizeof(status_text));
    strcpy(status_text, text);
    return true;
}
void usb_webusb_task(void) {
    if (exercise_main_loop) {
        if (task_calls++ == 2u) longjmp(loop_exit, 1);
        mock_time += OUTPUT_VOLTAGE_REFRESH_US;
    }
}

static void reset_voltage_cache(void) {
    g_output_voltage_valid = false;
    g_output_voltage_mv = 0;
    g_output_voltage_t_us = 0;
    voltage_reads = 0;
}

int main(void) {
    _Static_assert(sizeof(wave_frame_t) == 530u, "voltage must not change current frames");
    reset_voltage_cache();
    mock_time = 1000u;
    mock_vout_mv = 3300u;
    refresh_output_voltage(false);
    assert(voltage_reads == 1u && g_output_voltage_mv == 3300u);
    assert(g_output_voltage_t_us == 1032u);
    mock_time = 101031u;
    mock_vout_mv = 1800u;
    refresh_output_voltage(false);
    assert(voltage_reads == 1u && g_output_voltage_mv == 3300u);
    mock_time = 101032u;
    refresh_output_voltage(false);
    assert(voltage_reads == 2u && g_output_voltage_mv == 1800u);

    // GET_STATUS samples immediately instead of reporting the 100 ms cache.
    mock_time = 101100u;
    mock_vout_mv = 2900u;
    g_state = APP_RUNNING;
    char command[] = "GET_STATUS";
    handle_command(command);
    assert(voltage_reads == 3u);
    assert(strstr(status_text, "state=RUNNING") && strstr(status_text, "vdac_mv=2500"));
    assert(strstr(status_text, "vout_mv=2900 vout_t_us=101132 vout_age_us=0\n"));
    assert(strlen(status_text) < 512u && status_text[strlen(status_text) - 1u] == '\n');

    // Both the refresh interval and the reported timestamp survive timer wrap.
    reset_voltage_cache();
    mock_time = (uint64_t)UINT32_MAX - 16u;
    mock_vout_mv = 6600u;
    refresh_output_voltage(false);
    assert(g_output_voltage_t_us == 15u);
    mock_time += OUTPUT_VOLTAGE_REFRESH_US - 1u;
    refresh_output_voltage(false);
    assert(voltage_reads == 1u);
    mock_time++;
    refresh_output_voltage(false);
    assert(voltage_reads == 2u);
    mock_time = UINT32_MAX - 32u;
    send_status();
    assert(strstr(status_text, "vout_t_us=4294967295 vout_age_us=0\n"));
    assert(strlen(status_text) < 512u && status_text[strlen(status_text) - 1u] == '\n');

    // Execute the real main loop while sampling: voltage keeps refreshing,
    // OLED/local controls remain paused, and first read follows startup UI.
    reset_voltage_cache();
    mock_time = 0;
    mock_running = true;
    mock_vout_mv = 3300u;
    ui_updates = 0;
    task_calls = 0;
    exercise_main_loop = true;
    if (setjmp(loop_exit) == 0) (void)firmware_main();
    exercise_main_loop = false;
    assert(voltage_reads == 3u && ui_updates == 0u);
    assert(g_output_voltage_t_us >= 3200000u);
    puts("voltage telemetry: fresh measured STATUS, 100 ms live cache, timer wrap, text bounds and running UI isolation passed");
    return 0;
}
