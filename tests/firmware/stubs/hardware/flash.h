#pragma once
#include <stddef.h>
#include <stdint.h>
#define FLASH_SECTOR_SIZE 4096u
#define FLASH_PAGE_SIZE 256u
void flash_range_erase(uint32_t flash_offset, size_t count);
void flash_range_program(uint32_t flash_offset, const uint8_t *data, size_t count);
