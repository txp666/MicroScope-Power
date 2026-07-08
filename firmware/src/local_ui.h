#pragma once

#include "ads8866.h"

#include <stdint.h>

void local_ui_init(void);
void local_ui_update(uint32_t now_ms);
bool local_ui_take_output_save_request(void);
void local_ui_set_sense_milliohm(ads8866_range_t range, uint32_t milliohm);
uint32_t local_ui_get_sense_milliohm(ads8866_range_t range);
