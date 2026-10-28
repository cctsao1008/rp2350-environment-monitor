#include "sense_hat_console.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "sense_hat_marquee.h"
#include "sense_hat_sensors.h"

#define CMD_LENGTH 76u
static char command[CMD_LENGTH + 1u];
static size_t used;

static void help(void) {
    printf("LED commands: led demo | led TEXT | led b 1..4 | led off\r\n");
    printf("Green-only 8x8 marquee, one pass, automatically OFF. Default OFF.\r\n");
    printf("HTS221: hts diag (read-only) | hts oneshot (mode test + restore)\r\n");
    printf("HTS221: hts avgtest (temporary 0x1B averaging + restore)\r\n");
    printf("HTS221: hts dump (all defined registers, RAW and CAL; read-only)\r\n");
}

static void execute(const char *line) {
    if (strcmp(line, "help") == 0 || strcmp(line, "led help") == 0) {
        help();
        return;
    }
    if (strcmp(line, "hts dump") == 0) {
        sense_hts221_dump();
        return;
    }
    if (strcmp(line, "hts diag") == 0) {
        sense_hts221_diagnose(false);
        return;
    }
    if (strcmp(line, "hts avgtest") == 0) {
        sense_hts221_averaging_test();
        return;
    }
    if (strcmp(line, "hts oneshot") == 0) {
        sense_hts221_diagnose(true);
        return;
    }
    if (strncmp(line, "led", 3) != 0 || (line[3] != '\0' && line[3] != ' ')) {
        printf("Unknown command; type 'help'\r\n");
        return;
    }

    if (!sense_hat_marquee_available()) {
        printf("LED unavailable: Sense HAT V1 controller was not verified\r\n");
        return;
    }
    const char *arg = line + 3;
    while (*arg == ' ') ++arg;
    if (!*arg) { help(); return; }
    if (strcmp(arg, "off") == 0) {
        sense_hat_marquee_stop();
        printf("Sense HAT LED: OFF\r\n");
    } else if (strcmp(arg, "demo") == 0) {
        (void)sense_hat_marquee_start("HELLO RP2350");
        printf("Sense HAT LED: scrolling HELLO RP2350\r\n");
    } else if (arg[0] == 'b' && arg[1] == ' ' &&
               arg[2] >= '1' && arg[2] <= '4' && arg[3] == '\0') {
        (void)sense_hat_marquee_set_brightness((unsigned)(arg[2] - '0'));
        printf("Sense HAT LED: low brightness %c/31\r\n", arg[2]);
    } else {
        if (strncmp(arg, "text ", 5) == 0) arg += 5;
        if (!sense_hat_marquee_start(arg)) {
            printf("Invalid LED message (1..48 characters)\r\n");
        } else {
            printf("Sense HAT LED: scrolling %.48s\r\n", arg);
        }
    }
}

void sense_hat_console_poll(void) {
    for (unsigned i = 0; i < 32u; ++i) { // Don't starve sensor sampling
        int ch = getchar_timeout_us(0);
        if (ch == PICO_ERROR_TIMEOUT) break;

        if (ch == '\r' || ch == '\n') {
            if (used) {
                command[used] = '\0';
                execute(command);
                used = 0;
            }
        } else if (ch == '\b' || ch == 127) {
            if (used) --used;
        } else if (ch >= 32 && ch <= 126) {
            if (used < CMD_LENGTH) {
                command[used++] = (char)ch;
            } else {
                used = 0;
                printf("LED command too long\r\n");
            }
        }
    }
}
