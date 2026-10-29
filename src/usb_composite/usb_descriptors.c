/*
 * Composite USB descriptor, adapted from TinyUSB
 * examples/device/net_lwip_webserver/src/usb_descriptors.c (MIT).
 * Source version is pico-sdk 2.3.1's pinned TinyUSB commit.
 * Copyright (c) 2019 Ha Thach (tinyusb.org). MIT license applies.
 */
#include <string.h>
#include <stdint.h>
#include "tusb.h"
#include "class/net/net_device.h"
#include "pico/usb_reset.h"
#include "pico/unique_id.h"

// Developer-only VID/PID; not registered for a manufactured product.
// Different PID than CDC-only firmware to prevent Windows driver caching.
#define EXPERIMENT_VID 0xCAFE
#define EXPERIMENT_PID 0x40A3

enum { STR_LANG=0, STR_MANUF, STR_PRODUCT, STR_SERIAL, STR_NET,
       STR_MAC, STR_CONSOLE, STR_RESET };
enum { ITF_NET=0, ITF_NET_DATA=1, ITF_CDC=2, ITF_CDC_DATA=3,
       ITF_RESET=4, ITF_TOTAL=5 };

extern uint8_t tud_network_mac_address[6];

static const tusb_desc_device_t device_desc = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0201, // BOS support is needed for WINNCM binding.
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = EXPERIMENT_VID, .idProduct = EXPERIMENT_PID,
    .bcdDevice = 0x0100,
    .iManufacturer = STR_MANUF, .iProduct = STR_PRODUCT,
    .iSerialNumber = STR_SERIAL, .bNumConfigurations = 1
};
uint8_t const *tud_descriptor_device_cb(void) {
    return (uint8_t const *)&device_desc;
}

#define TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_NCM_DESC_LEN + \
                   TUD_CDC_DESC_LEN + TUD_RPI_RESET_DESC_LEN)
static const uint8_t config_desc[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_TOTAL, 0, TOTAL_LEN, 0, 100),
    // Network CDC-NCM (two interfaces): INT IN 0x81, BULK OUT/IN 0x02/0x82.
    // Pinned TinyUSB 2.3.1 SDK macro takes exactly 9 arguments.
    TUD_CDC_NCM_DESCRIPTOR(ITF_NET, STR_NET, STR_MAC,
        0x81, 64, 0x02, 0x82, 64, CFG_TUD_NET_MTU),
    // CDC ACM console (two interfaces): INT IN 0x83, BULK OUT/IN 0x04/0x84.
    TUD_CDC_DESCRIPTOR(ITF_CDC, STR_CONSOLE,
        0x83, 8, 0x04, 0x84, 64),
    // Endpoint-free Raspberry Pi vendor reset, picked up by pico_usb_reset.
    TUD_RPI_RESET_DESCRIPTOR(ITF_RESET, STR_RESET)
};
TU_VERIFY_STATIC(sizeof(config_desc) == TOTAL_LEN, "Composite descriptor length");
uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    return index == 0 ? config_desc : NULL;
}

// One BOS and one MS OS 2.0 set describe BOTH WINNCM and WINUSB.
// The SDK's own OS descriptor callback must be disabled at build time.
// In particular, do not omit the reset function subset.
#define BOS_TOTAL_LEN      (TUD_BOS_DESC_LEN + TUD_BOS_MICROSOFT_OS_DESC_LEN)

// Original pinned-TinyUSB WINNCM set is 0xB2 bytes; pico_usb_reset
// appends a separate RPI_RESET_MS_OS_20_DESC_LEN function subset.
// The configuration subset includes both functions, while the WINNCM
// function subset length stays at (0xB2 - 0x0A - 0x08).
#define NCM_MS_OS_20_DESC_LEN 0xB2
#define MS_OS_20_DESC_LEN (NCM_MS_OS_20_DESC_LEN + RPI_RESET_MS_OS_20_DESC_LEN)

// BOS carries one MS OS 2.0 platform capability for WINNCM + vendor reset.
uint8_t const desc_bos[] =
{
  // total length, number of device caps
  TUD_BOS_DESCRIPTOR(BOS_TOTAL_LEN, 1),

  // Microsoft OS 2.0 descriptor
  TUD_BOS_MS_OS_20_DESCRIPTOR(MS_OS_20_DESC_LEN, 1)
};

uint8_t const * tud_descriptor_bos_cb(void)
{
  return desc_bos;
}

uint8_t const desc_ms_os_20[] =
{
  // Set header: length, type, windows version, total length
  U16_TO_U8S_LE(0x000A), U16_TO_U8S_LE(MS_OS_20_SET_HEADER_DESCRIPTOR), U32_TO_U8S_LE(0x06030000), U16_TO_U8S_LE(MS_OS_20_DESC_LEN),

  // Configuration subset header: length, type, configuration index, reserved, configuration total length
  U16_TO_U8S_LE(0x0008), U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_CONFIGURATION), 0, 0, U16_TO_U8S_LE(MS_OS_20_DESC_LEN-0x0A),

  // Function Subset header: length, type, first interface, reserved, subset length
  U16_TO_U8S_LE(0x0008), U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_FUNCTION), ITF_NET, 0, U16_TO_U8S_LE(NCM_MS_OS_20_DESC_LEN-0x0A-0x08),

  // MS OS 2.0 Compatible ID descriptor: length, type, compatible ID, sub compatible ID
  U16_TO_U8S_LE(0x0014), U16_TO_U8S_LE(MS_OS_20_FEATURE_COMPATBLE_ID), 'W', 'I', 'N', 'N', 'C', 'M', 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // sub-compatible

  // MS OS 2.0 Registry property descriptor: length, type
  U16_TO_U8S_LE(NCM_MS_OS_20_DESC_LEN-0x0A-0x08-0x08-0x14), U16_TO_U8S_LE(MS_OS_20_FEATURE_REG_PROPERTY),
  U16_TO_U8S_LE(0x0007), U16_TO_U8S_LE(0x002A), // wPropertyDataType, wPropertyNameLength and PropertyName "DeviceInterfaceGUIDs\0" in UTF-16
  'D', 0x00, 'e', 0x00, 'v', 0x00, 'i', 0x00, 'c', 0x00, 'e', 0x00, 'I', 0x00, 'n', 0x00, 't', 0x00, 'e', 0x00,
  'r', 0x00, 'f', 0x00, 'a', 0x00, 'c', 0x00, 'e', 0x00, 'G', 0x00, 'U', 0x00, 'I', 0x00, 'D', 0x00, 's', 0x00, 0x00, 0x00,
  U16_TO_U8S_LE(0x0050), // wPropertyDataLength
	//bPropertyData: {12345678-0D08-43FD-8B3E-127CA8AFFF9D}
  '{', 0x00, '1', 0x00, '2', 0x00, '3', 0x00, '4', 0x00, '5', 0x00, '6', 0x00, '7', 0x00, '8', 0x00, '-', 0x00,
  '0', 0x00, 'D', 0x00, '0', 0x00, '8', 0x00, '-', 0x00, '4', 0x00, '3', 0x00, 'F', 0x00, 'D', 0x00, '-', 0x00,
  '8', 0x00, 'B', 0x00, '3', 0x00, 'E', 0x00, '-', 0x00, '1', 0x00, '2', 0x00, '7', 0x00, 'C', 0x00, 'A', 0x00,
  '8', 0x00, 'A', 0x00, 'F', 0x00, 'F', 0x00, 'F', 0x00, '9', 0x00, 'D', 0x00, '}', 0x00, 0x00, 0x00, 0x00, 0x00,
  RPI_RESET_MS_OS_20_DESCRIPTOR(ITF_RESET)
};

TU_VERIFY_STATIC(sizeof(desc_ms_os_20) == MS_OS_20_DESC_LEN, "Incorrect size");

// Invoked when a control transfer occurred on an interface of this class
// Driver response accordingly to the request and the transfer stage (setup/data/ack)
// return false to stall control endpoint (e.g unsupported request)
bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const* request) {
  // nothing to with DATA & ACK stage
  if (stage != CONTROL_STAGE_SETUP) return true;

  switch (request->bmRequestType_bit.type) {
    case TUSB_REQ_TYPE_VENDOR:
      switch (request->bRequest) {
        case 1:
          if (request->wIndex == 7) {
            // Get Microsoft OS 2.0 compatible descriptor
            uint16_t total_len;
            memcpy(&total_len, desc_ms_os_20 + 8, 2);

            return tud_control_xfer(rhport, request, (void*)(uintptr_t)desc_ms_os_20, total_len);
          } else {
            return false;
          }

        default: break;
      }
      break;

    default: break;
  }

  // stall unknown request
  return false;
}


static const char *const strings[] = {
    [STR_MANUF] = "RP2350 Sensor Experiment",
    [STR_PRODUCT] = "Environment Monitor NCM + CDC",
    [STR_NET] = "USB Network (NCM)",
    [STR_CONSOLE] = "Sensor Debug Console",
    [STR_RESET] = "RP2350 Vendor Reset"
};
static uint16_t string_buf[33];
uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    if (index == STR_LANG) {
        string_buf[0] = (TUSB_DESC_STRING << 8) | 4;
        string_buf[1] = 0x0409;
        return string_buf;
    }
    char serial[33] = {0};
    const char *s = NULL;
    size_t len=0;
    if (index == STR_SERIAL) {
        pico_get_unique_board_id_string(serial, sizeof(serial));
        s=serial;
    } else if (index == STR_MAC) {
        static const char digits[]="0123456789ABCDEF";
        for (unsigned i=0; i<6; ++i) {
            string_buf[1+i*2] = digits[tud_network_mac_address[i] >> 4];
            string_buf[2+i*2] = digits[tud_network_mac_address[i] & 0x0f];
        }
        len=12;
    } else if (index < sizeof(strings)/sizeof(strings[0])) {
        s=strings[index];
    }
    if (s) {
        len=strlen(s);
        if (len>32) len=32;
        for (size_t i=0;i<len;++i) string_buf[1+i]=(uint8_t)s[i];
    } else if(index != STR_MAC) return NULL;
    string_buf[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2*len+2));
    return string_buf;
}
