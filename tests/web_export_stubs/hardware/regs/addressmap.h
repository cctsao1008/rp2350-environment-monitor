#ifndef TEST_WEB_ADDRESSMAP_H
#define TEST_WEB_ADDRESSMAP_H
#include <stdint.h>
extern uint8_t test_nor_flash[12u * 1024u * 1024u];
#define XIP_BASE ((uintptr_t)test_nor_flash - 0x400000u)
#endif
