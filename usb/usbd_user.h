/*
 * Copyright (c) 2026 Zhangqi Li (@zhangqili)
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef __HID_KEYBOARD_H
#define __HID_KEYBOARD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "usbd_core.h"
#include "usbd_hid.h"

#define USBD_LANGID_STRING 1033

enum
{
    USB_STATE_IDLE = 0,
    USB_STATE_BUSY
};

typedef struct __USBBuffer
{
    uint8_t read_buffer[64];
    uint8_t send_buffer[64];
    uint8_t state;
} USBBuffer;

void usb_init(uint8_t busid, uintptr_t reg_base);
int usb_send_keyboard(uint8_t *buffer, uint8_t size);

#ifdef __cplusplus
}
#endif

#endif
