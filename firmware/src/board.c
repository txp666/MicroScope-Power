#include "board.h"

#include "bsp/board_api.h"

void app_board_init(void) {
    board_init();
}

void app_board_init_after_usb(void) {
    if (board_init_after_tusb) {
        board_init_after_tusb();
    }
}

void app_board_set_led(bool on) {
    board_led_write(on);
}

uint32_t app_board_millis(void) {
    return board_millis();
}
