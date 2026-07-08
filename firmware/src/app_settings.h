#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint16_t zero_code_1_ohm;
    uint16_t zero_code_10_ohm;
    uint32_t sense_1_mohm;
    uint32_t sense_10_mohm;
    uint16_t output_vdac_mv;
    uint16_t reserved;
} app_settings_t;

bool app_settings_load(app_settings_t *settings);
bool app_settings_save(const app_settings_t *settings);
