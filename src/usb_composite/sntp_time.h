#ifndef RP2350_SNTP_TIME_H
#define RP2350_SNTP_TIME_H
#include <stdbool.h>
#include <stdint.h>

/*
 * RP2350 itself performs SNTP over lwIP UDP/123. Windows is only an IPv4
 * forwarding/NAT hop; no Windows application supplies or sets the clock.
 *
 * Sensor timestamps remain monotonic. These are separate UTC anchors.
 */
typedef struct {
    uint64_t utc_us;
    uint64_t monotonic_us;
    uint32_t updates;
    uint32_t age_ms;
    bool synchronized;
} rp2350_utc_anchor_t;

void rp2350_sntp_time_init(void);
bool rp2350_sntp_anchor(rp2350_utc_anchor_t *out);
bool rp2350_sntp_now_utc_us(uint64_t *out);

/* Called only by lwIP SNTP core in the main-loop/lwIP context. */
void rp2350_sntp_apply(uint32_t unix_seconds, uint32_t microseconds);
void rp2350_sntp_request_nonce(int32_t *seconds, uint32_t *fraction);
#endif
