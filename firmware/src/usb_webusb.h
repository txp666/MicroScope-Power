#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "protocol.h"

#define USB_WEBUSB_VID 0xCafeu
#define USB_WEBUSB_PID 0x4012u
#define USB_WEBUSB_INTERFACE 0u
#define USB_WEBUSB_EP_OUT 0x01u
#define USB_WEBUSB_EP_IN 0x81u
#define USB_WEBUSB_EP_SIZE 64u

#define USB_CDC_EP_NOTIF 0x82u
#define USB_CDC_EP_OUT 0x02u
#define USB_CDC_EP_IN 0x83u
#define USB_CDC_EP_SIZE 64u

void usb_webusb_init(void);
void usb_webusb_task(void);
bool usb_webusb_connected(void);
bool usb_cdc_connected(void);

bool usb_webusb_read_command(char *out, uint32_t out_size);
bool usb_webusb_can_send_frame(void);
bool usb_webusb_send_frame(const wave_frame_t *frame);
// Commands are backpressured until a response slot is available. A false
// return means the session has ended and the host must reconnect.
bool usb_webusb_send_text(const char *text);
// Includes queued bytes/FIFO, but not an already submitted endpoint packet.
bool usb_webusb_tx_pending(void);
