#include "adc_voltage.h"
#include "ads8866.h"
#include "app_settings.h"
#include "board.h"
#include "local_ui.h"
#include "power_output.h"
#include "protocol.h"
#include "sampler.h"
#include "usb_webusb.h"

#include "pico/stdlib.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    APP_BOOT = 0,
    APP_USB_WAIT,
    APP_IDLE,
    APP_RUNNING,
    APP_FAULT,
} app_state_t;

static app_state_t g_state = APP_BOOT;
static bool g_settings_save_pending = false;

static const char *app_state_name(app_state_t state) {
    switch (state) {
    case APP_BOOT:
        return "BOOT";
    case APP_USB_WAIT:
        return "USB_WAIT";
    case APP_IDLE:
        return "IDLE";
    case APP_RUNNING:
        return "RUNNING";
    case APP_FAULT:
        return "FAULT";
    default:
        return "UNKNOWN";
    }
}

static void uppercase_command(char *cmd) {
    for (; *cmd; ++cmd) {
        *cmd = (char)toupper((unsigned char)*cmd);
    }
}

static bool parse_u32_arg(const char *text, uint32_t *value) {
    char *end = NULL;
    unsigned long parsed = strtoul(text, &end, 10);
    if (end == text || *end != '\0') {
        return false;
    }

    *value = (uint32_t)parsed;
    return true;
}

static bool parse_two_u32_args(const char *text, uint32_t *first, uint32_t *second) {
    char *end = NULL;
    unsigned long parsed_first = strtoul(text, &end, 10);
    if (end == text || *end != ' ') {
        return false;
    }

    while (*end == ' ') {
        end++;
    }

    char *second_end = NULL;
    unsigned long parsed_second = strtoul(end, &second_end, 10);
    if (second_end == end || *second_end != '\0') {
        return false;
    }

    *first = (uint32_t)parsed_first;
    *second = (uint32_t)parsed_second;
    return true;
}

static void send_ok(void) {
    usb_webusb_send_text("OK\n");
}

static void send_err(const char *code) {
    char line[48];
    snprintf(line, sizeof(line), "ERR %s\n", code);
    usb_webusb_send_text(line);
}

static app_settings_t current_settings(void) {
    app_settings_t settings = {
        .zero_code_1_ohm = sampler_get_zero_code_for_range(ADS8866_RANGE_1_OHM),
        .zero_code_10_ohm = sampler_get_zero_code_for_range(ADS8866_RANGE_10_OHM),
        .sense_1_mohm = local_ui_get_sense_milliohm(ADS8866_RANGE_1_OHM),
        .sense_10_mohm = local_ui_get_sense_milliohm(ADS8866_RANGE_10_OHM),
        .output_vdac_mv = power_output_vdac_mv(),
        .reserved = 0u,
    };
    return settings;
}

static void load_saved_settings(void) {
    app_settings_t settings;
    if (!app_settings_load(&settings)) {
        return;
    }

    sampler_set_zero_code_for_range(ADS8866_RANGE_1_OHM, settings.zero_code_1_ohm);
    sampler_set_zero_code_for_range(ADS8866_RANGE_10_OHM, settings.zero_code_10_ohm);
    local_ui_set_sense_milliohm(ADS8866_RANGE_1_OHM, settings.sense_1_mohm);
    local_ui_set_sense_milliohm(ADS8866_RANGE_10_OHM, settings.sense_10_mohm);
    if (settings.output_vdac_mv <= POWER_OUTPUT_VDAC_MAX_MV) {
        (void)power_output_set_vdac_mv(settings.output_vdac_mv);
    }
}

static bool save_current_settings(void) {
    app_settings_t settings = current_settings();
    return app_settings_save(&settings);
}

static void request_settings_save(void) {
    g_settings_save_pending = true;
}

static void send_status(void) {
    char line[352];
    snprintf(line, sizeof(line),
             "STATUS state=%s srate=%lu source=ADC dropped=%lu seq=%u zero=%u zero1=%u zero10=%u last=%u range=%u auto_range=%u sense1_mohm=%lu sense10_mohm=%lu vdac_mv=%u\n",
             app_state_name(g_state),
             (unsigned long)sampler_get_rate(),
             (unsigned long)sampler_get_dropped(),
             sampler_get_next_seq(),
             sampler_get_zero_code(),
             sampler_get_zero_code_for_range(ADS8866_RANGE_1_OHM),
             sampler_get_zero_code_for_range(ADS8866_RANGE_10_OHM),
             sampler_get_last_sample(),
             sampler_get_range() == ADS8866_RANGE_10_OHM ? 10u : 1u,
             sampler_get_auto_range() ? 1u : 0u,
             (unsigned long)local_ui_get_sense_milliohm(ADS8866_RANGE_1_OHM),
             (unsigned long)local_ui_get_sense_milliohm(ADS8866_RANGE_10_OHM),
             power_output_vdac_mv());
    usb_webusb_send_text(line);
}

static void handle_command(char *cmd) {
    uppercase_command(cmd);

    if (strcmp(cmd, "PING") == 0) {
        usb_webusb_send_text("OK PONG\n");
        return;
    }

    if (strcmp(cmd, "START") == 0) {
        if (g_state == APP_FAULT) {
            send_err("FAULT");
            return;
        }

        if (sampler_start()) {
            g_state = APP_RUNNING;
            send_ok();
        } else {
            g_state = APP_FAULT;
            send_err("SAMPLER");
        }
        return;
    }

    if (strcmp(cmd, "STOP") == 0 || strcmp(cmd, "RESET") == 0) {
        sampler_stop();
        g_state = usb_webusb_connected() ? APP_IDLE : APP_USB_WAIT;
        send_ok();
        return;
    }

    if (strncmp(cmd, "SRATE ", 6) == 0) {
        uint32_t rate = 0;
        if (!parse_u32_arg(cmd + 6, &rate) || !sampler_set_rate(rate)) {
            send_err("BAD_RATE");
            return;
        }
        send_ok();
        return;
    }

    if (strcmp(cmd, "ZERO_CAL") == 0) {
        sampler_set_zero_code(sampler_get_last_sample());
        request_settings_save();
        send_status();
        return;
    }

    if (strncmp(cmd, "SET_ZERO_CODE ", 14) == 0) {
        uint32_t zero = 0;
        if (!parse_u32_arg(cmd + 14, &zero) || zero > 65535u) {
            send_err("BAD_ZERO");
            return;
        }
        sampler_set_zero_code((uint16_t)zero);
        request_settings_save();
        send_ok();
        return;
    }

    if (strncmp(cmd, "SET_ZERO_CODE_RANGE ", 20) == 0) {
        uint32_t range = 0;
        uint32_t zero = 0;
        if (!parse_two_u32_args(cmd + 20, &range, &zero) ||
            (range != 1u && range != 10u) ||
            zero > 65535u) {
            send_err("BAD_ZERO_RANGE");
            return;
        }
        sampler_set_zero_code_for_range(range == 10u ? ADS8866_RANGE_10_OHM : ADS8866_RANGE_1_OHM,
                                        (uint16_t)zero);
        request_settings_save();
        send_ok();
        return;
    }

    const char sense_prefix[] = "SET_SENSE_MOHM_RANGE ";
    if (strncmp(cmd, sense_prefix, sizeof(sense_prefix) - 1u) == 0) {
        uint32_t range = 0;
        uint32_t milliohm = 0;
        if (!parse_two_u32_args(cmd + sizeof(sense_prefix) - 1u, &range, &milliohm) ||
            (range != 1u && range != 10u) ||
            milliohm == 0u ||
            milliohm > 1000000000u) {
            send_err("BAD_SENSE");
            return;
        }
        local_ui_set_sense_milliohm(range == 10u ? ADS8866_RANGE_10_OHM : ADS8866_RANGE_1_OHM,
                                    milliohm);
        request_settings_save();
        send_ok();
        return;
    }

    if (strncmp(cmd, "SET_AUTO_RANGE ", 15) == 0) {
        uint32_t enabled = 0;
        if (!parse_u32_arg(cmd + 15, &enabled) || enabled > 1u) {
            send_err("BAD_AUTO_RANGE");
            return;
        }
        sampler_set_auto_range(enabled != 0u);
        send_ok();
        return;
    }

    if (strncmp(cmd, "SET_RANGE ", 10) == 0) {
        uint32_t range = 0;
        if (!parse_u32_arg(cmd + 10, &range) || (range != 1u && range != 10u)) {
            send_err("BAD_RANGE");
            return;
        }
        if (!sampler_set_range(range == 10u ? ADS8866_RANGE_10_OHM : ADS8866_RANGE_1_OHM)) {
            send_err("RANGE");
            return;
        }
        send_ok();
        return;
    }

    if (strcmp(cmd, "GET_STATUS") == 0) {
        send_status();
        return;
    }

    send_err("BAD_CMD");
}

static void service_commands(void) {
    char cmd[PROTOCOL_CMD_MAX_LEN];
    while (usb_webusb_read_command(cmd, sizeof(cmd))) {
        handle_command(cmd);
    }
}

static void service_wave_tx(void) {
    if (g_state != APP_RUNNING || !usb_webusb_connected()) {
        return;
    }

    wave_frame_t frame;
    while (usb_webusb_can_send_frame()) {
        if (!sampler_pop_frame(&frame)) {
            return;
        }

        if (!usb_webusb_send_frame(&frame)) {
            return;
        }
    }
}

int main(void) {
    app_board_init();
    usb_webusb_init();
    power_output_init();
    adc_voltage_init();
    ads8866_init();
    load_saved_settings();
    local_ui_init();
    sampler_init();

    g_state = APP_USB_WAIT;

    while (true) {
        usb_webusb_task();

        if (!usb_webusb_connected()) {
            if (g_state == APP_RUNNING) {
                sampler_stop();
            }
            g_state = APP_USB_WAIT;
        } else if (g_state == APP_USB_WAIT) {
            g_state = APP_IDLE;
        }

        service_commands();
        service_wave_tx();
        if (!sampler_is_running()) {
            local_ui_update(to_ms_since_boot(get_absolute_time()));
            if (local_ui_take_output_save_request()) {
                request_settings_save();
            }
            if (g_settings_save_pending) {
                g_settings_save_pending = false;
                (void)save_current_settings();
            }
        }
        tight_loop_contents();
    }
}
