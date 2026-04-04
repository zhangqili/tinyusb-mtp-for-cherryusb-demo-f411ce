/*
 * Copyright (c) 2026 Zhangqi Li (@zhangqili)
 *
 * SPDX-License-Identifier: MIT
 */
#include "usbd_user.h"
#include "usbd_hid.h"
#include "usbd_mtp.h"
#include "main.h"
#include "usbd.h"
#include "mtp.h"


#define USBD_VID           0xffff
#define USBD_PID           0xffff
#define USBD_MAX_POWER     100
#define USBD_LANGID_STRING 1033

#define KEYBOARD_EPSIZE 8
#define KEYBOARD_EPIN_ADDR 0x81

#define EPNUM_MTP_EVT     0x82
#define EPNUM_MTP_OUT     0x03
#define EPNUM_MTP_IN      0x83

#define KEYBOARD_INTERFACE ITF_NUM_HID

#define HID_INT_EP          KEYBOARD_EPIN_ADDR
#define HID_INT_EP_SIZE     8
#define HID_INT_EP_INTERVAL 10



#define USB_CONFIG_SIZE       64
#define HID_KEYBOARD_REPORT_DESC_SIZE sizeof(hid_keyboard_report_desc)

#define USBD_MSOS_VENDOR_CODE 0x20


static const uint8_t hid_keyboard_report_desc[] = {
    0x05, 0x01, // USAGE_PAGE (Generic Desktop)
    0x09, 0x06, // USAGE (Keyboard)
    0xa1, 0x01, // COLLECTION (Application)
    0x05, 0x07, // USAGE_PAGE (Keyboard)
    0x19, 0xe0, // USAGE_MINIMUM (Keyboard LeftControl)
    0x29, 0xe7, // USAGE_MAXIMUM (Keyboard Right GUI)
    0x15, 0x00, // LOGICAL_MINIMUM (0)
    0x25, 0x01, // LOGICAL_MAXIMUM (1)
    0x75, 0x01, // REPORT_SIZE (1)
    0x95, 0x08, // REPORT_COUNT (8)
    0x81, 0x02, // INPUT (Data,Var,Abs)
    0x95, 0x01, // REPORT_COUNT (1)
    0x75, 0x08, // REPORT_SIZE (8)
    0x81, 0x03, // INPUT (Cnst,Var,Abs)
    0x95, 0x05, // REPORT_COUNT (5)
    0x75, 0x01, // REPORT_SIZE (1)
    0x05, 0x08, // USAGE_PAGE (LEDs)
    0x19, 0x01, // USAGE_MINIMUM (Num Lock)
    0x29, 0x05, // USAGE_MAXIMUM (Kana)
    0x91, 0x02, // OUTPUT (Data,Var,Abs)
    0x95, 0x01, // REPORT_COUNT (1)
    0x75, 0x03, // REPORT_SIZE (3)
    0x91, 0x03, // OUTPUT (Cnst,Var,Abs)
    0x95, 0x06, // REPORT_COUNT (6)
    0x75, 0x08, // REPORT_SIZE (8)
    0x15, 0x00, // LOGICAL_MINIMUM (0)
    0x25, 0xFF, // LOGICAL_MAXIMUM (255)
    0x05, 0x07, // USAGE_PAGE (Keyboard)
    0x19, 0x00, // USAGE_MINIMUM (Reserved (no event indicated))
    0x29, 0x65, // USAGE_MAXIMUM (Keyboard Application)
    0x81, 0x00, // INPUT (Data,Ary,Abs)
    0xc0        // END_COLLECTION
};

static const uint8_t msosv1_string_descriptor[] = {
    0x12,                       /* bLength */
    0x03,                       /* bDescriptorType */
    'M', 0, 'S', 0, 'F', 0, 'T', 0, '1', 0, '0', 0, '0', 0, /* qwSignature "MSFT100" */
    USBD_MSOS_VENDOR_CODE,      /* bMS_VendorCode */
    0x00                        /* bPad */
};


static const uint8_t device_descriptor[] = {
    USB_DEVICE_DESCRIPTOR_INIT(USB_2_0, 0x00, 0x00, 0x00, USBD_VID, USBD_PID, 0x0002, 0x01)
};

enum
{
  ITF_NUM_HID = 0,
  ITF_NUM_MTP,
  ITF_NUM_TOTAL
};

static const uint8_t config_descriptor[] = {
    USB_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, ITF_NUM_TOTAL, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    HID_KEYBOARD_DESCRIPTOR_INIT(ITF_NUM_HID, 0x01, HID_KEYBOARD_REPORT_DESC_SIZE, HID_INT_EP, HID_INT_EP_SIZE, HID_INT_EP_INTERVAL),
    TUD_MTP_DESCRIPTOR(ITF_NUM_MTP, 4, EPNUM_MTP_EVT, 64, 1, EPNUM_MTP_OUT, EPNUM_MTP_IN, 64),
};


static const uint8_t device_quality_descriptor[] = {
    ///////////////////////////////////////
    /// device qualifier descriptor
    ///////////////////////////////////////
    0x0a,
    USB_DESCRIPTOR_TYPE_DEVICE_QUALIFIER,
    0x00,
    0x02,
    0x00,
    0x00,
    0x00,
    0x40,
    0x00,
    0x00,
};

static const char *string_descriptors[] = {
    (const char[]){ 0x09, 0x04 }, /* Langid */
    "CherryUSB",                  /* Manufacturer */
    "CherryUSB HID DEMO",         /* Product */
    "2022123456",                 /* Serial Number */
    "MTP",
};

static const uint8_t *device_descriptor_callback(uint8_t speed)
{
    UNUSED(speed);

    return (const uint8_t *)device_descriptor;
}

static const uint8_t *config_descriptor_callback(uint8_t speed)
{
    UNUSED(speed);

    return (const uint8_t *)config_descriptor;
}

static const uint8_t *device_quality_descriptor_callback(uint8_t speed)
{
    return device_quality_descriptor;
}

static const char *string_descriptor_callback(uint8_t speed, uint8_t index)
{
    (void)speed;
    if (index == 0xEE) {
        return (const char *)msosv1_string_descriptor;
    }
    if (index >= (sizeof(string_descriptors) / sizeof(char *))) {
        return NULL;
    }
    return string_descriptors[index];
}

const struct usb_descriptor usb_descriptor = {
    .device_descriptor_callback = device_descriptor_callback,
    .config_descriptor_callback = config_descriptor_callback,
    .device_quality_descriptor_callback = device_quality_descriptor_callback,
    .string_descriptor_callback = string_descriptor_callback,
};

void usbd_hid_get_report(uint8_t busid, uint8_t intf, uint8_t report_id, uint8_t report_type, uint8_t **data, uint32_t *len)
{
    UNUSED(busid);
    UNUSED(intf);
    UNUSED(report_id);
    UNUSED(report_type);
    UNUSED(data);
    UNUSED(len);
    switch (intf)
    {
    default:
        (*data[0]) = 0;
        *len = 1;
        break;
    }
}

void usbd_hid_set_report(uint8_t busid, uint8_t intf, uint8_t report_id, uint8_t report_type, uint8_t *report, uint32_t report_len)
{
    UNUSED(busid);
    UNUSED(report_type);
    if (intf == KEYBOARD_INTERFACE)
    {
        if (report_len == 1)
        {
            HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, report[1]|0x02 ? GPIO_PIN_SET : GPIO_PIN_RESET);
        }
    }
}




/* Store example melody as an array of note values */
static volatile bool keyboard_state;
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t keyboard_buffer[KEYBOARD_EPSIZE];

static void usbd_hid_keyboard_in_callback(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    UNUSED(busid);
    UNUSED(ep);
    UNUSED(nbytes);
    keyboard_state = USB_STATE_IDLE;
}

static struct usbd_interface keyboard_intf;
static struct usbd_endpoint keyboard_in_ep = {
    .ep_cb = usbd_hid_keyboard_in_callback,
    .ep_addr = KEYBOARD_EPIN_ADDR
};
    
static struct usbd_interface mtp_intf;

static void usbd_event_handler(uint8_t busid, uint8_t event)
{
    UNUSED(busid);
    switch (event)
    {
    case USBD_EVENT_RESET:
        keyboard_state = USB_STATE_IDLE;
        break;
    case USBD_EVENT_CONNECTED:
        break;
    case USBD_EVENT_DISCONNECTED:
        break;
    case USBD_EVENT_RESUME:
        break;
    case USBD_EVENT_SUSPEND:
        break;
    case USBD_EVENT_CONFIGURED:
        break;
    case USBD_EVENT_SET_REMOTE_WAKEUP:
        break;
    case USBD_EVENT_CLR_REMOTE_WAKEUP:
        break;
    case USBD_EVENT_SOF:
        break;
    default:
        break;
    }
}

void usb_init(uint8_t busid, uintptr_t reg_base)
{
    usbd_desc_register(0, &usb_descriptor);
    
    usbd_add_interface(0, usbd_hid_init_intf(0, &keyboard_intf, hid_keyboard_report_desc, sizeof(hid_keyboard_report_desc)));
    usbd_add_endpoint(0, &keyboard_in_ep);

    usbd_add_interface(0, usbd_mtp_init_intf(0, &mtp_intf, config_descriptor + USB_CONFIG_SIZE - 30 ,
        30,
        EPNUM_MTP_OUT, 
        EPNUM_MTP_IN,
        EPNUM_MTP_EVT));

    usbd_initialize(busid, reg_base, usbd_event_handler);
}

int usb_send_keyboard(uint8_t *buffer, uint8_t size)
{
    UNUSED(size);
    if (keyboard_state == USB_STATE_BUSY)
    {
        return 1;
    }
    keyboard_state = USB_STATE_BUSY;
    memcpy(keyboard_buffer, buffer, KEYBOARD_EPSIZE);
    int ret = usbd_ep_start_write(0, KEYBOARD_EPIN_ADDR, keyboard_buffer, KEYBOARD_EPSIZE);
    if (ret < 0)
    {
        keyboard_state = USB_STATE_IDLE;
        return 1;
    }
    return 0;
}