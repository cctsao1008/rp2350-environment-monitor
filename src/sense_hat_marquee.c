#include "sense_hat_marquee.h"

#include <stdint.h>
#include <string.h>
#include "pico/time.h"

#define LED_I2C_ADDR 0x46u
#define FRAME_SIZE 192u
#define CHUNK_SIZE 16u
#define GLYPH_WIDTH 6u
#define DISPLAY_WIDTH 8u
#define TEXT_CAPACITY 48u
#define STEP_PERIOD_MS 170u
#define I2C_TIMEOUT_US 30000u

static i2c_inst_t *led_bus;
static char text[TEXT_CAPACITY + 1u];
static uint32_t next_step_ms;
static unsigned scroll_column;
static unsigned brightness = 2u; // 2/31 full-scale green; deliberately dim
static bool running;

static const uint8_t digits[10][5] = {
    {0x3E,0x51,0x49,0x45,0x3E}, // 0
    {0x00,0x42,0x7F,0x40,0x00}, // 1
    {0x42,0x61,0x51,0x49,0x46}, // 2
    {0x21,0x41,0x45,0x4B,0x31}, // 3
    {0x18,0x14,0x12,0x7F,0x10}, // 4
    {0x27,0x45,0x45,0x45,0x39}, // 5
    {0x3C,0x4A,0x49,0x49,0x30}, // 6
    {0x01,0x71,0x09,0x05,0x03}, // 7
    {0x36,0x49,0x49,0x49,0x36}, // 8
    {0x06,0x49,0x49,0x29,0x1E}  // 9
};
static const uint8_t letters[26][5] = {
    {0x7E,0x11,0x11,0x11,0x7E}, // A
    {0x7F,0x49,0x49,0x49,0x36}, // B
    {0x3E,0x41,0x41,0x41,0x22}, // C
    {0x7F,0x41,0x41,0x22,0x1C}, // D
    {0x7F,0x49,0x49,0x49,0x41}, // E
    {0x7F,0x09,0x09,0x09,0x01}, // F
    {0x3E,0x41,0x49,0x49,0x7A}, // G
    {0x7F,0x08,0x08,0x08,0x7F}, // H
    {0x00,0x41,0x7F,0x41,0x00}, // I
    {0x20,0x40,0x41,0x3F,0x01}, // J
    {0x7F,0x08,0x14,0x22,0x41}, // K
    {0x7F,0x40,0x40,0x40,0x40}, // L
    {0x7F,0x02,0x0C,0x02,0x7F}, // M
    {0x7F,0x04,0x08,0x10,0x7F}, // N
    {0x3E,0x41,0x41,0x41,0x3E}, // O
    {0x7F,0x09,0x09,0x09,0x06}, // P
    {0x3E,0x41,0x51,0x21,0x5E}, // Q
    {0x7F,0x09,0x19,0x29,0x46}, // R
    {0x46,0x49,0x49,0x49,0x31}, // S
    {0x01,0x01,0x7F,0x01,0x01}, // T
    {0x3F,0x40,0x40,0x40,0x3F}, // U
    {0x1F,0x20,0x40,0x20,0x1F}, // V
    {0x7F,0x20,0x18,0x20,0x7F}, // W
    {0x63,0x14,0x08,0x14,0x63}, // X
    {0x07,0x08,0x70,0x08,0x07}, // Y
    {0x61,0x51,0x49,0x45,0x43}  // Z
};
static const uint8_t blank[5] = {0,0,0,0,0};
static const uint8_t dot[5] = {0,0x60,0x60,0,0};
static const uint8_t dash[5] = {0x08,0x08,0x08,0x08,0x08};
static const uint8_t colon[5] = {0,0x36,0x36,0,0};
static const uint8_t percent[5] = {0x63,0x13,0x08,0x64,0x63};
static const uint8_t slash[5] = {0x20,0x10,0x08,0x04,0x02};
static const uint8_t question[5] = {0x02,0x01,0x51,0x09,0x06};

static const uint8_t *glyph(char ch) {
    if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
    if (ch >= '0' && ch <= '9') return digits[(unsigned)(ch - '0')];
    if (ch >= 'A' && ch <= 'Z') return letters[(unsigned)(ch - 'A')];
    switch (ch) {
        case ' ': return blank;
        case '.': return dot;
        case '-': return dash;
        case ':': return colon;
        case '%': return percent;
        case '/': return slash;
        default: return question;
    }
}

static bool push_frame(const uint8_t frame[FRAME_SIZE]) {
    uint8_t tx[CHUNK_SIZE + 1u];
    for (unsigned offset = 0u; offset < FRAME_SIZE; offset += CHUNK_SIZE) {
        tx[0] = (uint8_t)offset;
        memcpy(&tx[1], frame + offset, CHUNK_SIZE);
        if (i2c_write_timeout_us(led_bus, LED_I2C_ADDR, tx, sizeof(tx),
                                 false, I2C_TIMEOUT_US) != (int)sizeof(tx))
            return false;
    }
    return true;
}

void sense_hat_marquee_init(i2c_inst_t *bus, bool controller_verified) {
    led_bus = controller_verified ? bus : NULL;
    running = false;
    scroll_column = 0u;
    text[0] = '\0';
}

bool sense_hat_marquee_available(void) { return led_bus != NULL; }

bool sense_hat_marquee_start(const char *message) {
    if (!led_bus || !message || !message[0]) return false;
    size_t n = strlen(message);
    if (n > TEXT_CAPACITY) return false;
    memcpy(text, message, n + 1u);
    scroll_column = 0u;
    next_step_ms = to_ms_since_boot(get_absolute_time());
    running = true;
    return true;
}

void sense_hat_marquee_stop(void) {
    running = false;
    if (led_bus) {
        const uint8_t dark[FRAME_SIZE] = {0};
        (void)push_frame(dark);
    }
}

bool sense_hat_marquee_set_brightness(unsigned level) {
    if (level < 1u || level > 4u) return false;
    brightness = level;
    return true;
}

void sense_hat_marquee_poll(void) {
    if (!running || !led_bus) return;
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if ((int32_t)(now - next_step_ms) < 0) return;
    next_step_ms = now + STEP_PERIOD_MS;

    const unsigned message_pixels = (unsigned)strlen(text) * GLYPH_WIDTH;
    if (scroll_column > message_pixels + DISPLAY_WIDTH) {
        sense_hat_marquee_stop();
        return;
    }

    uint8_t frame[FRAME_SIZE] = {0};
    for (unsigned x = 0u; x < DISPLAY_WIDTH; ++x) {
        const int global_column = (int)(scroll_column + x) - (int)DISPLAY_WIDTH;
        if (global_column < 0 || (unsigned)global_column >= message_pixels)
            continue;
        const unsigned glyph_index = (unsigned)global_column / GLYPH_WIDTH;
        const unsigned glyph_column = (unsigned)global_column % GLYPH_WIDTH;
        if (glyph_column >= 5u) continue; // one-column character spacing
        const uint8_t column = glyph(text[glyph_index])[glyph_column];
        for (unsigned y = 0u; y < 7u; ++y) {
            if (column & (uint8_t)(1u << y)) {
                // ATtiny88 framebuffer layout: 8 rows * [R(8), G(8), B(8)].
                // Green-only, restricted to 1..4 of the 31 hardware levels.
                frame[y * 24u + 8u + x] = (uint8_t)brightness;
            }
        }
    }
    if (!push_frame(frame)) {
        running = false; // No retry storm if the Sense HAT is removed.
    }
    ++scroll_column;
}
