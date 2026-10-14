/* Exercise the real Flash settings implementation against an XIP memory image.
 * Hardware hooks verify ordering and inject failed/corrupt Flash operations. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define PICO_FLASH_SIZE_BYTES (2u * 4096u)
_Alignas(uint32_t) uint8_t test_flash_storage[PICO_FLASH_SIZE_BYTES];
#include "../../firmware/src/app_settings.c"

static unsigned operation;
static unsigned erase_count;
static unsigned program_count;
static bool core_1_parked;
static bool interrupts_enabled;
static bool initial_interrupts_enabled;
static bool corrupt_program;
static bool ignore_flash_operations;

void multicore_lockout_start_blocking(void) {
    assert(operation++ == 0);
    assert(!core_1_parked);
    assert(interrupts_enabled == initial_interrupts_enabled);
    core_1_parked = true;
}

uint32_t save_and_disable_interrupts(void) {
    assert(operation++ == 1);
    assert(core_1_parked);
    uint32_t previous = interrupts_enabled ? 1u : 0u;
    interrupts_enabled = false;
    return previous;
}

void flash_range_erase(uint32_t offset, size_t count) {
    assert(operation++ == 2);
    assert(core_1_parked && !interrupts_enabled);
    assert(offset == APP_SETTINGS_FLASH_OFFSET && count == FLASH_SECTOR_SIZE);
    ++erase_count;
    if (!ignore_flash_operations) { memset(test_flash_storage + offset, 0xff, count); }
}

void flash_range_program(uint32_t offset, const uint8_t *data, size_t count) {
    assert(operation++ == 3);
    assert(core_1_parked && !interrupts_enabled);
    assert(offset == APP_SETTINGS_FLASH_OFFSET && count == FLASH_PAGE_SIZE);
    for (size_t i = sizeof(app_settings_record_t); i < count; ++i) { assert(data[i] == 0xff); }
    ++program_count;
    if (!ignore_flash_operations) {
        memcpy(test_flash_storage + offset, data, count);
        if (corrupt_program) { test_flash_storage[offset + sizeof(app_settings_record_t) - 1] ^= 1u; }
    }
}

void restore_interrupts(uint32_t status) {
    assert(operation++ == 4);
    assert(core_1_parked && !interrupts_enabled);
    assert(status == (initial_interrupts_enabled ? 1u : 0u));
    interrupts_enabled = status != 0;
}

void multicore_lockout_end_blocking(void) {
    assert(operation++ == 5);
    assert(core_1_parked);
    assert(interrupts_enabled == initial_interrupts_enabled);
    core_1_parked = false;
}

static void prepare_call(bool enable_interrupts) {
    operation = 0;
    core_1_parked = false;
    interrupts_enabled = enable_interrupts;
    initial_interrupts_enabled = enable_interrupts;
}

static void assert_completed_write(void) {
    assert(operation == 6);
    assert(!core_1_parked && interrupts_enabled == initial_interrupts_enabled);
    for (unsigned i = 0; i < APP_SETTINGS_FLASH_OFFSET; ++i) { assert(test_flash_storage[i] == 0xa5); }
}

int main(void) {
    memset(test_flash_storage, 0xa5, sizeof(test_flash_storage));
    memset(test_flash_storage + APP_SETTINGS_FLASH_OFFSET, 0xff, FLASH_SECTOR_SIZE);
    app_settings_t settings = {
        .zero_code_1_ohm = 123,
        .zero_code_10_ohm = 456,
        .sense_1_mohm = 1003,
        .sense_10_mohm = 9980,
        .output_vdac_mv = 1200,
        .reserved = 0,
    };
    app_settings_t loaded;

    prepare_call(true);
    assert(!app_settings_load(NULL));
    assert(!app_settings_load(&loaded));
    assert(!app_settings_save(NULL));
    assert(operation == 0 && erase_count == 0 && program_count == 0);

    assert(app_settings_save(&settings));
    assert_completed_write();
    assert(app_settings_load(&loaded));
    assert(memcmp(&loaded, &settings, sizeof(settings)) == 0);
    assert(erase_count == 1 && program_count == 1);

    /* The no-change fast path must preserve Flash endurance and IRQ state. */
    prepare_call(true);
    assert(app_settings_save(&settings));
    assert(operation == 0 && erase_count == 1 && program_count == 1);
    assert(interrupts_enabled && !core_1_parked);

    /* Preserve a caller's already-disabled IRQ state as well. */
    prepare_call(false);
    settings.output_vdac_mv = 1300;
    assert(app_settings_save(&settings));
    assert_completed_write();
    assert(app_settings_load(&loaded) && loaded.output_vdac_mv == 1300);

    /* A still-valid old record is not proof that the requested write worked. */
    prepare_call(true);
    ignore_flash_operations = true;
    settings.output_vdac_mv = 1400;
    assert(!app_settings_save(&settings));
    assert_completed_write();
    assert(app_settings_load(&loaded) && loaded.output_vdac_mv == 1300);
    ignore_flash_operations = false;

    prepare_call(true);
    corrupt_program = true;
    assert(!app_settings_save(&settings));
    assert_completed_write();
    assert(!app_settings_load(&loaded));
    corrupt_program = false;

    /* A failed readback must leave synchronization restored for a retry. */
    prepare_call(true);
    assert(app_settings_save(&settings));
    assert_completed_write();
    assert(app_settings_load(&loaded) && memcmp(&loaded, &settings, sizeof(settings)) == 0);
    assert(erase_count == 5 && program_count == 5);
    puts("settings: dual-core/IRQ Flash protection, unchanged-save skip, exact readback and failure recovery passed");
    return 0;
}
