#ifndef RP2350_SENSOR_TUSB_CONFIG_H
#define RP2350_SENSOR_TUSB_CONFIG_H
#include "lwipopts.h"
#define CFG_TUSB_OS OPT_OS_NONE
#define CFG_TUSB_DEBUG 0
#define CFG_TUD_ENABLED 1
#define CFG_TUD_ENDPOINT0_SIZE 64
#define CFG_TUD_CDC 1
#define CFG_TUD_CDC_RX_BUFSIZE 512
#define CFG_TUD_CDC_TX_BUFSIZE 1024
#define CFG_TUD_ECM_RNDIS 0
#define CFG_TUD_NCM 1
#define CFG_TUD_NET_MTU 1514
// TinyUSB NCM buffers need at least one full-size Ethernet frame.
#define CFG_TUD_NCM_IN_NTB_MAX_SIZE 3200
#define CFG_TUD_NCM_OUT_NTB_MAX_SIZE 3200
#define CFG_TUD_NCM_IN_NTB_N 1
#define CFG_TUD_NCM_OUT_NTB_N 1
#endif
