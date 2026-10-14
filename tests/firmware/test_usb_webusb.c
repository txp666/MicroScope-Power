#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../firmware/src/usb_webusb.c"

static uint8_t output[32768];
static uint32_t output_length;
static uint32_t tx_available;
static uint32_t max_write;
static uint8_t input[2048];
static uint32_t input_length, input_offset;
static bool mounted = true;

size_t board_usb_get_serial(uint16_t *buffer, size_t max_chars) { (void)buffer; (void)max_chars; return 0; }
bool stdio_usb_init(void) { return true; }
bool stdio_usb_connected(void) { return false; }
void app_board_init_after_usb(void) {}
void app_board_set_led(bool on) { (void)on; }
bool tud_init(uint8_t rhport) { (void)rhport; return true; }
void tud_task(void) {}
bool tud_mounted(void) { return mounted; }
uint32_t tud_vendor_write_available(void) { return tx_available; }
uint32_t tud_vendor_write_flush(void) { return 0; }
uint32_t tud_vendor_write(const void *buffer, uint32_t length) {
    if (length > tx_available) length = tx_available;
    if (length > max_write) length = max_write;
    assert(output_length + length <= sizeof(output));
    memcpy(output + output_length, buffer, length);
    output_length += length;
    tx_available -= length;
    return length;
}
uint32_t tud_vendor_available(void) { return input_length - input_offset; }
uint32_t tud_vendor_read(void *buffer, uint32_t length) {
    if (length > tud_vendor_available()) length = tud_vendor_available();
    memcpy(buffer, input + input_offset, length);
    input_offset += length;
    return length;
}
void tud_vendor_read_flush(void) { input_length = input_offset = 0; }
bool tud_control_xfer(uint8_t rhport, const tusb_control_request_t *request, void *buffer, uint16_t length) {
    (void)rhport; (void)request; (void)buffer; (void)length; return true;
}
bool tud_control_status(uint8_t rhport, const tusb_control_request_t *request) {
    (void)rhport; (void)request; return true;
}

static void set_connected(bool connected) {
    tusb_control_request_t request = { .bmRequestType_bit.type = TUSB_REQ_TYPE_CLASS, .bRequest = 0x22, .wValue = connected };
    assert(tud_vendor_control_xfer_cb(0, CONTROL_STAGE_SETUP, &request));
}

static void start_session(void) {
    // The browser performs a USB device reset before establishing a new session.
    tud_umount_cb();
    output_length = 0;
    tx_available = CFG_TUD_VENDOR_TX_BUFSIZE;
    max_write = UINT32_MAX;
    mounted = true;
    tud_mount_cb();
    set_connected(true);
}

static void feed(const char *text) {
    assert(strlen(text) <= sizeof(input));
    memcpy(input, text, strlen(text));
    input_length = strlen(text);
    input_offset = 0;
    tud_vendor_rx_cb(0, NULL, 0);
}

static void test_response_backpressure(void) {
    start_session();
    tx_available = 0;
    assert(usb_webusb_send_text("OK\n"));
    assert(output_length == 0 && usb_webusb_tx_pending());
    tx_available = 2;
    usb_webusb_task();
    assert(output_length == 2 && g_text_count == 1 && !usb_webusb_can_send_frame());
    tx_available = CFG_TUD_VENDOR_TX_BUFSIZE;
    usb_webusb_task();
    assert(output_length == 3 && memcmp(output, "OK\n", 3) == 0 && g_text_count == 0);

    start_session();
    tx_available = 0;
    feed("A\nB\nC\nD\nE\nF\nG\nH\nI\nJ\n");
    char command[64];
    for (unsigned i = 0; i < USB_CMD_QUEUE_DEPTH; i++) {
        assert(usb_webusb_read_command(command, sizeof(command)));
        assert(command[0] == (char)('A' + i));
        assert(usb_webusb_send_text("OK\n"));
    }
    usb_webusb_task();
    assert(!usb_webusb_read_command(command, sizeof(command)));
    assert(g_cmd_count == 2 && g_text_count == 8);
    tx_available = CFG_TUD_VENDOR_TX_BUFSIZE;
    usb_webusb_task();
    assert(output_length == 24);
    assert(usb_webusb_read_command(command, sizeof(command)) && strcmp(command, "I") == 0);
    assert(usb_webusb_read_command(command, sizeof(command)) && strcmp(command, "J") == 0);
}

static void test_frame_text_order(void) {
    start_session();
    wave_frame_t frame;
    memset(&frame, 0xa5, sizeof(frame));
    max_write = 100;
    assert(usb_webusb_send_frame(&frame));
    assert(output_length == 100 && g_frame_pending);
    assert(usb_webusb_send_text("OK\n"));
    while (g_frame_pending || g_text_count) usb_webusb_task();
    assert(output_length == sizeof(frame) + 3);
    assert(memcmp(output, &frame, sizeof(frame)) == 0);
    assert(memcmp(output + sizeof(frame), "OK\n", 3) == 0);

    start_session();
    tx_available = 0;
    assert(usb_webusb_send_text("STATUS state=RUNNING\n"));
    tx_available = CFG_TUD_VENDOR_TX_BUFSIZE;
    assert(usb_webusb_send_frame(&frame));
    const size_t line_length = strlen("STATUS state=RUNNING\n");
    assert(memcmp(output, "STATUS state=RUNNING\n", line_length) == 0);
    assert(memcmp(output + line_length, &frame, sizeof(frame)) == 0);
}

static void test_reset_and_overflow(void) {
    start_session();
    char oversized[100];
    memset(oversized, 'A', sizeof(oversized));
    memcpy(oversized + 80, "STOP\n", 6);
    feed(oversized);
    char command[64];
    assert(usb_webusb_read_command(command, sizeof(command)) && strcmp(command, "?") == 0);
    assert(!usb_webusb_read_command(command, sizeof(command)));
    feed("START\nSTOP\nPARTIAL");
    tx_available = 0;
    assert(usb_webusb_send_text("OK\n"));
    set_connected(false);
    assert(g_cmd_count == 0 && g_text_count == 0 && g_line_len == 0 && !g_frame_pending);
    start_session();
    feed("PING\n");
    assert(usb_webusb_read_command(command, sizeof(command)) && strcmp(command, "PING") == 0);
    tx_available = 0;
    for (unsigned i = 0; i < USB_TEXT_QUEUE_DEPTH; i++) assert(usb_webusb_send_text("OK\n"));
    assert(!usb_webusb_send_text("OK\n"));
    assert(!usb_webusb_connected() && g_cmd_count == 0 && g_text_count == 0);
}

static void test_bus_reset_without_unmount(void) {
    start_session();
    feed("START\nSTOP\nPARTIAL");
    tx_available = 0;
    assert(usb_webusb_send_text("OK\n"));
    assert(g_cmd_count == 2 && g_text_count == 1 && g_line_len != 0);

    // Pico SDK TinyUSB bus reset clears its FIFOs without tud_umount_cb.
    // SET_CONFIGURATION later invokes tud_mount_cb before the host's 0x22.
    output_length = 0;
    tx_available = CFG_TUD_VENDOR_TX_BUFSIZE;
    tud_mount_cb();
    usb_webusb_task();
    char command[64];
    assert(!usb_webusb_connected());
    assert(!usb_webusb_read_command(command, sizeof(command)));
    assert(output_length == 0 && g_text_count == 0 && g_cmd_count == 0 && g_line_len == 0);
    set_connected(true);
    feed("PING\n");
    assert(usb_webusb_read_command(command, sizeof(command)) && strcmp(command, "PING") == 0);
    assert(!usb_webusb_read_command(command, sizeof(command)));
}

int main(void) {
    test_response_backpressure();
    test_frame_text_order();
    test_reset_and_overflow();
    test_bus_reset_without_unmount();
    puts("usb_webusb: TX-full response retention, command backpressure, partial-frame ordering, reconnect/bus-reset and overflow passed");
    return 0;
}
