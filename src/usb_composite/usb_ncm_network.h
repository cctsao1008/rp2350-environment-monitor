#ifndef RP2350_USB_NCM_NETWORK_H
#define RP2350_USB_NCM_NETWORK_H
#include <stdbool.h>
void usb_ncm_network_identity_init(void);
bool usb_ncm_network_init(void);
void usb_ncm_network_service(void);
#endif
