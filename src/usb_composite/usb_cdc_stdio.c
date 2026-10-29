#include <stddef.h>
#include <stdint.h>
#include "tusb.h"
#include "pico/stdio.h"
#include "pico/stdio/driver.h"
#include "usb_cdc_stdio.h"

// Optional diagnostics. USB is serviced on the main loop, not from an IRQ.
// Avoid indefinitely blocking sensor acquisition when the terminal is closed.
static void out_chars(const char *buf, int len) {
    if (!tud_cdc_n_connected(0) || len <= 0) return;
    while (len > 0) {
        uint32_t avail=tud_cdc_n_write_available(0);
        if (avail == 0) break; // drop overflow; sampling takes priority
        uint32_t count=(uint32_t)len < avail ? (uint32_t)len : avail;
        uint32_t written=tud_cdc_n_write(0, buf, count);
        if (!written) break;
        buf += written;
        len -= (int)written;
    }
    (void)tud_cdc_n_write_flush(0);
}

static void flush(void) {
    if (tud_cdc_n_connected(0)) (void)tud_cdc_n_write_flush(0);
}

static int in_chars(char *buf, int len) {
    if (!tud_cdc_n_connected(0) || len <= 0) return PICO_ERROR_NO_DATA;
    uint32_t n=tud_cdc_n_read(0,buf,(uint32_t)len);
    return n ? (int)n : PICO_ERROR_NO_DATA;
}

static stdio_driver_t cdc_stdio_driver = {
    .out_chars=out_chars, .out_flush=flush, .in_chars=in_chars
};

void usb_cdc_stdio_init(void) {
    stdio_set_driver_enabled(&cdc_stdio_driver, true);
}
