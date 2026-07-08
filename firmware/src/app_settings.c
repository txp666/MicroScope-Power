#include "app_settings.h"

#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"
#include "pico/multicore.h"

#include <string.h>

#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (2u * 1024u * 1024u)
#endif

#define APP_SETTINGS_MAGIC 0x50535730u
#define APP_SETTINGS_VERSION 1u
#define APP_SETTINGS_FLASH_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)

typedef struct {
    uint32_t magic;
    uint32_t version;
    app_settings_t settings;
    uint32_t checksum;
} app_settings_record_t;

static uint32_t checksum_bytes(const uint8_t *data, uint32_t len) {
    uint32_t hash = 2166136261u;
    for (uint32_t i = 0; i < len; ++i) {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

static uint32_t record_checksum(const app_settings_record_t *record) {
    return checksum_bytes((const uint8_t *)record,
                          (uint32_t)(sizeof(*record) - sizeof(record->checksum)));
}

static const app_settings_record_t *stored_record(void) {
    return (const app_settings_record_t *)(XIP_BASE + APP_SETTINGS_FLASH_OFFSET);
}

static bool record_valid(const app_settings_record_t *record) {
    return record->magic == APP_SETTINGS_MAGIC &&
           record->version == APP_SETTINGS_VERSION &&
           record->checksum == record_checksum(record);
}

bool app_settings_load(app_settings_t *settings) {
    const app_settings_record_t *record = stored_record();
    if (!settings || !record_valid(record)) {
        return false;
    }

    *settings = record->settings;
    return true;
}

bool app_settings_save(const app_settings_t *settings) {
    if (!settings) {
        return false;
    }

    app_settings_record_t record;
    memset(&record, 0xff, sizeof(record));
    record.magic = APP_SETTINGS_MAGIC;
    record.version = APP_SETTINGS_VERSION;
    record.settings = *settings;
    record.checksum = record_checksum(&record);

    const app_settings_record_t *current = stored_record();
    if (record_valid(current) && memcmp(&current->settings, settings, sizeof(*settings)) == 0) {
        return true;
    }

    uint8_t page[FLASH_PAGE_SIZE];
    memset(page, 0xff, sizeof(page));
    memcpy(page, &record, sizeof(record));

    multicore_lockout_start_blocking();
    flash_range_erase(APP_SETTINGS_FLASH_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(APP_SETTINGS_FLASH_OFFSET, page, FLASH_PAGE_SIZE);
    multicore_lockout_end_blocking();

    return record_valid(stored_record());
}
