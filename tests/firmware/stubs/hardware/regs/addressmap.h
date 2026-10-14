#pragma once
#include <stdint.h>
extern uint8_t test_flash_storage[];
#define XIP_BASE ((uintptr_t)test_flash_storage)
