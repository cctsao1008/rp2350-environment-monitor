/*
 * RP2350 SNTP client. This module runs on RP2350; Windows is only L3 NAT.
 * No system wall-clock changes, no writes from the network into Flash.
 *
 * lwIP SNTP checks matching source + request originate (CHECK_RESPONSE=2).
 * Standard unauthenticated SNTP is not cryptographically trusted (no NTS).
 * Without a synchronized local RTC, the server's transmit timestamp is
 * anchored at receipt; its one-way transit delay is not compensated.
 */
#include "sntp_time.h"

#include <string.h>
#include "lwip/apps/sntp.h"
#include "pico/rand.h"
#include "pico/time.h"

#define MIN_UNIX_SECONDS 1577836800u /* 2020-01-01 */
#define MAX_UNIX_SECONDS 4102444800u /* 2100-01-01 */
#define MAX_ANCHOR_AGE_US (6ull * 60ull * 60ull * 1000000ull)

static rp2350_utc_anchor_t current;
static bool initialized;

void rp2350_sntp_request_nonce(int32_t *seconds, uint32_t *fraction) {
    if (!seconds || !fraction) return;
    /* lwIP uses this only as a request/response nonce, not for time
       compensation (SNTP_COMP_ROUNDTRIP=0). Avoid predictable boot-zero. */
    *seconds = (int32_t)get_rand_32();
    *fraction = get_rand_32();
}

void rp2350_sntp_apply(uint32_t unix_seconds, uint32_t microseconds) {
    if (unix_seconds < MIN_UNIX_SECONDS ||
        unix_seconds >= MAX_UNIX_SECONDS || microseconds >= 1000000u)
        return;
    const uint64_t now = time_us_64();
    current.utc_us = (uint64_t)unix_seconds * 1000000ull + microseconds;
    current.monotonic_us = now;
    current.updates++;
    current.age_ms = 0;
    current.synchronized = true;
}

bool rp2350_sntp_anchor(rp2350_utc_anchor_t *out) {
    if (!out) return false;
    *out = current;
    if (!current.synchronized) return false;
    const uint64_t now = time_us_64();
    if (now < current.monotonic_us ||
        now-current.monotonic_us > MAX_ANCHOR_AGE_US) {
        out->synchronized = false;
        return false;
    }
    out->age_ms = (uint32_t)((now-current.monotonic_us)/1000u);
    return true;
}

bool rp2350_sntp_now_utc_us(uint64_t *out) {
    if (!out) return false;
    rp2350_utc_anchor_t anchor;
    if (!rp2350_sntp_anchor(&anchor)) return false;
    *out = anchor.utc_us + (time_us_64() - anchor.monotonic_us);
    return true;
}

void rp2350_sntp_time_init(void) {
    if (initialized) return;
    memset(&current,0,sizeof(current));
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0,"time.cloudflare.com");
    sntp_init();
    initialized=true;
}
