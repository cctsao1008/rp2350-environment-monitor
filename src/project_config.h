#ifndef RP2350_ENVIRONMENT_MONITOR_PROJECT_CONFIG_H
#define RP2350_ENVIRONMENT_MONITOR_PROJECT_CONFIG_H

// CMake ENABLE_SENSE_HAT_V1 is the single build-time switch.
// The default is OFF because the Raspberry Pi Sense HAT V1 was removed.
// Keep the safe fallback when this header is used outside CMake.
#ifndef PROJECT_ENABLE_SENSE_HAT_V1
#define PROJECT_ENABLE_SENSE_HAT_V1 0
#endif

#ifndef PROJECT_ENABLE_USB_WEB_PHASE1
#define PROJECT_ENABLE_USB_WEB_PHASE1 0
#endif

#if PROJECT_ENABLE_SENSE_HAT_V1 != 0 && PROJECT_ENABLE_SENSE_HAT_V1 != 1
#error "PROJECT_ENABLE_SENSE_HAT_V1 must be either 0 or 1"
#endif

#endif
