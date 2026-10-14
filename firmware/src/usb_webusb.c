#include "usb_webusb.h"

#include "board.h"

#include "bsp/board_api.h"
#include "pico/stdio_usb.h"
#include "tusb.h"

#include <stdint.h>
#include <string.h>

#define VENDOR_REQUEST_WEBUSB 1u
#define VENDOR_REQUEST_MICROSOFT 2u
#define MS_OS_20_DESC_LEN 0xB2u
#define BOS_TOTAL_LEN (TUD_BOS_DESC_LEN + TUD_BOS_WEBUSB_DESC_LEN + TUD_BOS_MICROSOFT_OS_DESC_LEN)
#define USB_BCD 0x0210u

#define USB_CMD_QUEUE_DEPTH 8u
#define USB_TEXT_QUEUE_DEPTH 8u
#define USB_TEXT_MAX_LEN 512u

static volatile bool g_mounted = false;
static volatile bool g_webusb_connected = false;

static char g_cmd_queue[USB_CMD_QUEUE_DEPTH][PROTOCOL_CMD_MAX_LEN];
static uint8_t g_cmd_write = 0;
static uint8_t g_cmd_read = 0;
static uint8_t g_cmd_count = 0;
static char g_line_buf[PROTOCOL_CMD_MAX_LEN];
static uint8_t g_line_len = 0;
static bool g_line_overflow = false;

typedef struct {
    char bytes[USB_TEXT_MAX_LEN];
    uint16_t length;
    uint16_t sent;
} text_response_t;

static text_response_t g_text_queue[USB_TEXT_QUEUE_DEPTH];
static uint8_t g_text_write = 0;
static uint8_t g_text_read = 0;
static uint8_t g_text_count = 0;
static wave_frame_t g_partial_frame;
static uint16_t g_frame_sent = 0;
static bool g_frame_pending = false;

static void reset_session_buffers(void) {
    g_cmd_write = g_cmd_read = g_cmd_count = 0;
    g_line_len = 0;
    g_line_overflow = false;
    g_text_write = g_text_read = g_text_count = 0;
    g_frame_sent = 0;
    g_frame_pending = false;
    tud_vendor_read_flush();
    // TinyUSB bundled with Pico SDK has no public TX-clear API. The WebUSB
    // client resets the USB device before opening a new command session; that
    // bus reset also clears any old TinyUSB FIFO/endpoint bytes.
}

static void drain_tx(void) {
    if (!usb_webusb_connected()) return;

    // Finish a short frame write before inserting any text into the byte stream.
    if (g_frame_pending) {
        const uint8_t *bytes = (const uint8_t *)&g_partial_frame;
        g_frame_sent += tud_vendor_write(bytes + g_frame_sent, sizeof(g_partial_frame) - g_frame_sent);
        tud_vendor_write_flush();
        if (g_frame_sent < sizeof(g_partial_frame)) return;
        g_frame_pending = false;
    }

    while (g_text_count) {
        text_response_t *response = &g_text_queue[g_text_read];
        response->sent += tud_vendor_write(response->bytes + response->sent, response->length - response->sent);
        tud_vendor_write_flush();
        if (response->sent < response->length) return;
        g_text_read = (uint8_t)((g_text_read + 1u) % USB_TEXT_QUEUE_DEPTH);
        g_text_count--;
    }
}

static tusb_desc_device_t const desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = USB_BCD,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = USB_WEBUSB_VID,
    .idProduct = USB_WEBUSB_PID,
    .bcdDevice = 0x0100,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01,
};

uint8_t const *tud_descriptor_device_cb(void) {
    return (uint8_t const *)&desc_device;
}

enum {
    ITF_NUM_VENDOR = 0,
    ITF_NUM_CDC,
    ITF_NUM_CDC_DATA,
    ITF_NUM_TOTAL,
};

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_VENDOR_DESC_LEN + TUD_CDC_DESC_LEN)

static uint8_t const desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
    TUD_VENDOR_DESCRIPTOR(ITF_NUM_VENDOR, 0x04, USB_WEBUSB_EP_OUT, USB_WEBUSB_EP_IN, USB_WEBUSB_EP_SIZE),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 0x05, USB_CDC_EP_NOTIF, 8, USB_CDC_EP_OUT, USB_CDC_EP_IN, USB_CDC_EP_SIZE),
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_configuration;
}

static uint8_t const desc_bos[] = {
    TUD_BOS_DESCRIPTOR(BOS_TOTAL_LEN, 2),
    TUD_BOS_WEBUSB_DESCRIPTOR(VENDOR_REQUEST_WEBUSB, 1),
    TUD_BOS_MS_OS_20_DESCRIPTOR(MS_OS_20_DESC_LEN, VENDOR_REQUEST_MICROSOFT),
};

uint8_t const *tud_descriptor_bos_cb(void) {
    return desc_bos;
}

static uint8_t const desc_ms_os_20[] = {
    U16_TO_U8S_LE(0x000A), U16_TO_U8S_LE(MS_OS_20_SET_HEADER_DESCRIPTOR),
    U32_TO_U8S_LE(0x06030000), U16_TO_U8S_LE(MS_OS_20_DESC_LEN),

    U16_TO_U8S_LE(0x0008), U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_CONFIGURATION),
    0, 0, U16_TO_U8S_LE(MS_OS_20_DESC_LEN - 0x0A),

    U16_TO_U8S_LE(0x0008), U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_FUNCTION),
    ITF_NUM_VENDOR, 0, U16_TO_U8S_LE(MS_OS_20_DESC_LEN - 0x0A - 0x08),

    U16_TO_U8S_LE(0x0014), U16_TO_U8S_LE(MS_OS_20_FEATURE_COMPATBLE_ID),
    'W', 'I', 'N', 'U', 'S', 'B', 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

    U16_TO_U8S_LE(MS_OS_20_DESC_LEN - 0x0A - 0x08 - 0x08 - 0x14),
    U16_TO_U8S_LE(MS_OS_20_FEATURE_REG_PROPERTY),
    U16_TO_U8S_LE(0x0007), U16_TO_U8S_LE(0x002A),

    'D', 0x00, 'e', 0x00, 'v', 0x00, 'i', 0x00,
    'c', 0x00, 'e', 0x00, 'I', 0x00, 'n', 0x00,
    't', 0x00, 'e', 0x00, 'r', 0x00, 'f', 0x00,
    'a', 0x00, 'c', 0x00, 'e', 0x00, 'G', 0x00,
    'U', 0x00, 'I', 0x00, 'D', 0x00, 's', 0x00,
    0x00, 0x00,

    U16_TO_U8S_LE(0x0050),
    '{', 0x00, '4', 0x00, '0', 0x00, '4', 0x00,
    '0', 0x00, 'D', 0x00, 'D', 0x00, 'D', 0x00,
    'D', 0x00, '-', 0x00, '4', 0x00, '5', 0x00,
    '4', 0x00,
    '7', 0x00, '-', 0x00, '4', 0x00, 'F', 0x00,
    '2', 0x00, '1', 0x00, '-', 0x00, '9', 0x00,
    '2', 0x00, 'C', 0x00, '6', 0x00, '-', 0x00,
    '6', 0x00, '2', 0x00, '6', 0x00, 'E', 0x00,
    '0', 0x00, '6', 0x00, '4', 0x00, '2', 0x00,
    'D', 0x00, '4', 0x00, 'A', 0x00, 'D', 0x00,
    '}', 0x00, 0x00, 0x00, 0x00, 0x00,
};

TU_VERIFY_STATIC(sizeof(desc_ms_os_20) == MS_OS_20_DESC_LEN, "Incorrect MS OS 2.0 descriptor size");

#define LANDING_URL "modusignal.cn/pages/devices/microscope-power/"
static tusb_desc_webusb_url_t const desc_url = {
    .bLength = 3 + sizeof(LANDING_URL) - 1,
    .bDescriptorType = 3,
    .bScheme = 1,
    .url = LANDING_URL,
};

enum {
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,
    STRID_VENDOR_INTERFACE,
    STRID_CDC_INTERFACE,
};

static char const *const string_desc_arr[] = {
    (const char[]){0x09, 0x04},
    "PowerScope",
    "Pico Power Scope V0",
    NULL,
    "WebUSB Waveform",
    "USB CDC Log",
};

static uint16_t desc_str[32 + 1];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;

    size_t chr_count = 0;

    switch (index) {
    case STRID_LANGID:
        memcpy(&desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
        break;

    case STRID_SERIAL:
        chr_count = board_usb_get_serial(desc_str + 1, 32);
        break;

    default:
        if (index >= (sizeof(string_desc_arr) / sizeof(string_desc_arr[0]))) {
            return NULL;
        }

        const char *str = string_desc_arr[index];
        chr_count = strlen(str);
        if (chr_count > 32) {
            chr_count = 32;
        }

        for (size_t i = 0; i < chr_count; ++i) {
            desc_str[1 + i] = str[i];
        }
        break;
    }

    desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2u * chr_count + 2u));
    return desc_str;
}

static void cmd_queue_push(const char *line, uint8_t len) {
    if (len == 0) {
        return;
    }

    // RX draining stops at a full queue, leaving unread bytes in TinyUSB's FIFO.
    if (g_cmd_count >= USB_CMD_QUEUE_DEPTH) return;

    if (len >= PROTOCOL_CMD_MAX_LEN) {
        len = PROTOCOL_CMD_MAX_LEN - 1u;
    }

    memcpy(g_cmd_queue[g_cmd_write], line, len);
    g_cmd_queue[g_cmd_write][len] = '\0';
    g_cmd_write = (uint8_t)((g_cmd_write + 1u) % USB_CMD_QUEUE_DEPTH);
    g_cmd_count++;
}

static void process_rx_byte(uint8_t ch) {
    if (ch == '\r') {
        return;
    }

    if (ch == '\n') {
        // Reject the entire overlong command instead of executing its suffix.
        if (g_line_overflow) cmd_queue_push("?", 1);
        else cmd_queue_push(g_line_buf, g_line_len);
        g_line_len = 0;
        g_line_overflow = false;
        return;
    }

    if (g_line_overflow) return;
    if (g_line_len < (PROTOCOL_CMD_MAX_LEN - 1u)) {
        g_line_buf[g_line_len++] = (char)ch;
    } else {
        g_line_overflow = true;
    }
}

static void drain_rx(void) {
    if (!usb_webusb_connected()) return;
    while (g_cmd_count < USB_CMD_QUEUE_DEPTH && tud_vendor_available()) {
        uint8_t byte;
        if (tud_vendor_read(&byte, 1) != 1) break;
        process_rx_byte(byte);
    }
}

void usb_webusb_init(void) {
    tud_init(BOARD_TUD_RHPORT);
    app_board_init_after_usb();
    stdio_usb_init();
}

void usb_webusb_task(void) {
    tud_task();
    drain_tx();
    drain_rx();
}

bool usb_webusb_connected(void) {
    return tud_mounted() && g_mounted && g_webusb_connected;
}

bool usb_cdc_connected(void) {
    return stdio_usb_connected();
}

bool usb_webusb_read_command(char *out, uint32_t out_size) {
    // Each command produces one response. Reserve room before executing it.
    if (!usb_webusb_connected() || g_cmd_count == 0 || out_size == 0 || g_text_count >= USB_TEXT_QUEUE_DEPTH) {
        return false;
    }

    strncpy(out, g_cmd_queue[g_cmd_read], out_size - 1u);
    out[out_size - 1u] = '\0';
    g_cmd_read = (uint8_t)((g_cmd_read + 1u) % USB_CMD_QUEUE_DEPTH);
    g_cmd_count--;
    return true;
}

bool usb_webusb_can_send_frame(void) {
    drain_tx();
    return usb_webusb_connected() && !g_frame_pending && g_text_count == 0 &&
           tud_vendor_write_available() >= sizeof(wave_frame_t);
}

bool usb_webusb_send_frame(const wave_frame_t *frame) {
    if (!usb_webusb_can_send_frame()) {
        return false;
    }

    uint32_t written = tud_vendor_write(frame, sizeof(*frame));
    if (written < sizeof(*frame)) {
        g_partial_frame = *frame;
        g_frame_sent = (uint16_t)written;
        g_frame_pending = true;
    }
    tud_vendor_write_flush();
    return true;
}

bool usb_webusb_send_text(const char *text) {
    if (!usb_webusb_connected()) {
        return false;
    }

    drain_tx();
    uint32_t len = (uint32_t)strlen(text);
    if (len >= USB_TEXT_MAX_LEN || g_text_count >= USB_TEXT_QUEUE_DEPTH) {
        // Never silently lose an ACK and then accept another command in its
        // place. An impossible/unsolicited overflow requires a fresh session.
        g_webusb_connected = false;
        reset_session_buffers();
        app_board_set_led(false);
        return false;
    }

    text_response_t *response = &g_text_queue[g_text_write];
    memcpy(response->bytes, text, len);
    response->length = (uint16_t)len;
    response->sent = 0;
    g_text_write = (uint8_t)((g_text_write + 1u) % USB_TEXT_QUEUE_DEPTH);
    g_text_count++;
    drain_tx();
    return true;
}

bool usb_webusb_tx_pending(void) {
    return usb_webusb_connected() && (g_frame_pending || g_text_count != 0 ||
                                     tud_vendor_write_available() < CFG_TUD_VENDOR_TX_BUFSIZE);
}

void tud_mount_cb(void) {
    // TinyUSB does not invoke tud_umount_cb for a USB bus reset. A fresh
    // configuration must therefore clear old commands and wait for 0x22 too.
    g_webusb_connected = false;
    reset_session_buffers();
    app_board_set_led(false);
    g_mounted = true;
}

void tud_umount_cb(void) {
    g_mounted = false;
    g_webusb_connected = false;
    reset_session_buffers();
}

void tud_suspend_cb(bool remote_wakeup_en) {
    (void)remote_wakeup_en;
}

void tud_resume_cb(void) {
}

bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request) {
    if (stage != CONTROL_STAGE_SETUP) {
        return true;
    }

    switch (request->bmRequestType_bit.type) {
    case TUSB_REQ_TYPE_VENDOR:
        if (request->bRequest == VENDOR_REQUEST_WEBUSB) {
            return tud_control_xfer(rhport, request, (void *)(uintptr_t)&desc_url, desc_url.bLength);
        }

        if (request->bRequest == VENDOR_REQUEST_MICROSOFT && request->wIndex == 7) {
            uint16_t total_len = 0;
            memcpy(&total_len, desc_ms_os_20 + 8, sizeof(total_len));
            return tud_control_xfer(rhport, request, (void *)(uintptr_t)desc_ms_os_20, total_len);
        }
        break;

    case TUSB_REQ_TYPE_CLASS:
        if (request->bRequest == 0x22) {
            reset_session_buffers();
            g_webusb_connected = request->wValue != 0;
            app_board_set_led(g_webusb_connected);

            return tud_control_status(rhport, request);
        }
        break;

    default:
        break;
    }

    return false;
}

void tud_vendor_rx_cb(uint8_t idx, const uint8_t *buffer, uint16_t bufsize) {
    (void)idx;
    (void)buffer;
    (void)bufsize;

    drain_rx();
}
