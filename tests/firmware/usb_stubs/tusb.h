#pragma once
#include <stdbool.h>
#include <stdint.h>

// USB descriptors are compiled by the real firmware build. The host harness
// mocks only the external TinyUSB API and executes the real application queues.
#define BOARD_TUD_RHPORT 0
#define CFG_TUD_ENDPOINT0_SIZE 64
#define CFG_TUD_VENDOR_TX_BUFSIZE 4096
#define TUD_BOS_DESC_LEN 5
#define TUD_BOS_WEBUSB_DESC_LEN 24
#define TUD_BOS_MICROSOFT_OS_DESC_LEN 28
#define TUD_CONFIG_DESC_LEN 9
#define TUD_VENDOR_DESC_LEN 23
#define TUD_CDC_DESC_LEN 66
#define TUSB_DESC_DEVICE 1
#define TUSB_DESC_STRING 3
#define TUSB_CLASS_MISC 0xef
#define MISC_SUBCLASS_COMMON 2
#define MISC_PROTOCOL_IAD 1
#define MS_OS_20_SET_HEADER_DESCRIPTOR 0
#define MS_OS_20_SUBSET_HEADER_CONFIGURATION 1
#define MS_OS_20_SUBSET_HEADER_FUNCTION 2
#define MS_OS_20_FEATURE_COMPATBLE_ID 3
#define MS_OS_20_FEATURE_REG_PROPERTY 4
#define U16_TO_U8S_LE(v) ((v) & 0xff), (((v) >> 8) & 0xff)
#define U32_TO_U8S_LE(v) U16_TO_U8S_LE(v), U16_TO_U8S_LE((v) >> 16)
#define TUD_CONFIG_DESCRIPTOR(...) 0
#define TUD_VENDOR_DESCRIPTOR(...) 0
#define TUD_CDC_DESCRIPTOR(...) 0
#define TUD_BOS_DESCRIPTOR(...) 0
#define TUD_BOS_WEBUSB_DESCRIPTOR(...) 0
#define TUD_BOS_MS_OS_20_DESCRIPTOR(...) 0
#define TU_VERIFY_STATIC(condition, message) _Static_assert(condition, message)
#define CONTROL_STAGE_SETUP 0
#define TUSB_REQ_TYPE_VENDOR 2
#define TUSB_REQ_TYPE_CLASS 1

typedef struct {
    uint8_t bLength, bDescriptorType;
    uint16_t bcdUSB;
    uint8_t bDeviceClass, bDeviceSubClass, bDeviceProtocol, bMaxPacketSize0;
    uint16_t idVendor, idProduct, bcdDevice;
    uint8_t iManufacturer, iProduct, iSerialNumber, bNumConfigurations;
} tusb_desc_device_t;
typedef struct { uint8_t bLength, bDescriptorType, bScheme; char url[64]; } tusb_desc_webusb_url_t;
typedef struct {
    struct { uint8_t type; } bmRequestType_bit;
    uint8_t bRequest;
    uint16_t wValue, wIndex;
} tusb_control_request_t;

bool tud_init(uint8_t rhport);
void tud_task(void);
bool tud_mounted(void);
uint32_t tud_vendor_write(const void *buffer, uint32_t length);
uint32_t tud_vendor_write_available(void);
uint32_t tud_vendor_write_flush(void);
uint32_t tud_vendor_available(void);
uint32_t tud_vendor_read(void *buffer, uint32_t length);
void tud_vendor_read_flush(void);
bool tud_control_xfer(uint8_t rhport, const tusb_control_request_t *request, void *buffer, uint16_t length);
bool tud_control_status(uint8_t rhport, const tusb_control_request_t *request);
