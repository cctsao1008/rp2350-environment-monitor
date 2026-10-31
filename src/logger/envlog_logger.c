#include "envlog_logger.h"
#include "envlog_codec.h"
#include <string.h>
#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"
#include "pico/flash.h"

// This module is linked only for explicit ENABLE_BINARY_FLASH_LOGGER=ON.
// It must never program below 4 MiB or outside the 12 MiB logging partition.
// Single-core application only: flash_safe_execute disables interrupts and
// ensures that XIP is not touched while the external NOR is busy.

#if PICO_FLASH_SIZE_BYTES != (16u * 1024u * 1024u)
#error "Binary log requires verified Waveshare RP2350-PiZero 16 MiB flash."
#endif
#if ENVLOG_PARTITION_OFFSET % FLASH_SECTOR_SIZE != 0
#error "Environment logger partition must start at a sector boundary."
#endif
#if ENVLOG_PARTITION_OFFSET + ENVLOG_PARTITION_BYTES > PICO_FLASH_SIZE_BYTES
#error "Environment logger partition exceeds physical flash."
#endif

typedef struct {
    uint32_t offset;
    const uint8_t *data;
    bool erase;
} flash_request_t;

static bool active;
static bool sector_ready;
static bool found_any;
static uint32_t latest_sector;
static uint32_t next_sector;
static uint32_t active_sector;
static uint32_t generation;
static uint32_t session_id;
static uint32_t page_index; // 1..15, index 0 contains sector metadata
static uint32_t dropped;
static uint32_t last_uptime_ms;
static uint64_t uptime_wrap_ms;
static envlog_sample_t pending[ENVLOG_RECORDS_PER_PAGE];
static uint8_t pending_count;
static bool flush_partial;
static uint8_t flash_page[ENVLOG_PAGE_BYTES];

static const uint8_t *xip(uint32_t absolute_offset) {
    return (const uint8_t *)(uintptr_t)(XIP_BASE + absolute_offset);
}
static uint32_t sector_offset(uint32_t index) {
    return ENVLOG_PARTITION_OFFSET + index * ENVLOG_SECTOR_BYTES;
}
static bool all_ff(const uint8_t *p, size_t n) {
    for (size_t i=0; i<n; ++i) if (p[i] != 0xffu) return false;
    return true;
}
static bool sector_header_valid_at(uint32_t index) {
    return envlog_sector_header_valid(xip(sector_offset(index)));
}
static void __not_in_flash_func(flash_operation)(void *argument) {
    flash_request_t *op=(flash_request_t *)argument;
    if (op->erase) flash_range_erase(op->offset, ENVLOG_SECTOR_BYTES);
    else flash_range_program(op->offset, op->data, ENVLOG_PAGE_BYTES);
}
static bool safe_flash(uint32_t offset, const uint8_t *p, bool erase) {
    // Guard again at the actual operation boundary.
    if (offset < ENVLOG_PARTITION_OFFSET ||
        offset >= ENVLOG_PARTITION_OFFSET + ENVLOG_PARTITION_BYTES ||
        (erase && offset % ENVLOG_SECTOR_BYTES != 0u) ||
        (!erase && offset % ENVLOG_PAGE_BYTES != 0u) ||
        (erase && offset > ENVLOG_PARTITION_OFFSET +
                        ENVLOG_PARTITION_BYTES - ENVLOG_SECTOR_BYTES) ||
        (!erase && offset > ENVLOG_PARTITION_OFFSET +
                         ENVLOG_PARTITION_BYTES - ENVLOG_PAGE_BYTES))
        return false;
    flash_request_t request={.offset=offset,.data=p,.erase=erase};
    const int status=flash_safe_execute(flash_operation, &request, 1000u);
    if (status != PICO_OK) return false;
    // Check after XIP access is restored; do not read flash inside callback.
    return erase ? all_ff(xip(offset), ENVLOG_SECTOR_BYTES)
                 : memcmp(xip(offset), p, ENVLOG_PAGE_BYTES) == 0;
}
static bool select_and_open_sector(void) {
    for (uint32_t attempts=0; attempts<ENVLOG_SECTOR_COUNT; ++attempts) {
        const uint32_t index=(next_sector+attempts)%ENVLOG_SECTOR_COUNT;
        const uint32_t off=sector_offset(index);
        const uint8_t *head=xip(off);
        const bool empty=all_ff(head, ENVLOG_SECTOR_BYTES);
        const bool known=!empty && sector_header_valid_at(index);
        // Never erase arbitrary/non-logger data or the newest valid sector.
        if (!empty && (!known || (found_any && index == latest_sector)))
            continue;
        if (known && !safe_flash(off, NULL, true)) return false;
        envlog_make_sector_header(flash_page, generation, session_id);
        if (!safe_flash(off, flash_page, false)) return false;
        active_sector=index;
        latest_sector=index;
        found_any=true;
        next_sector=(index+1u)%ENVLOG_SECTOR_COUNT;
        ++generation; // Next sector's unique, modulo-32-bit generation.
        page_index=1u;
        sector_ready=true;
        return true;
    }
    // All sectors contain unrecognized data; fail closed, no destructive
    // formatting or blind erase. User must explicitly inspect flash offline.
    return false;
}

void envlog_logger_init(void) {
    active=true; sector_ready=false; found_any=false;
    latest_sector=0; next_sector=0; active_sector=0;
    generation=1; session_id=1; page_index=1;
    dropped=0; last_uptime_ms=0; uptime_wrap_ms=0;
    pending_count=0; flush_partial=false;
    // Only inspect metadata: fast bounded scan, no flash modification.
    for (uint32_t i=0; i<ENVLOG_SECTOR_COUNT; ++i) {
        if (!sector_header_valid_at(i)) continue;
        const uint8_t *h=xip(sector_offset(i));
        const uint32_t gen=envlog_read_u32(h+8u);
        if (!found_any || (int32_t)(gen-(generation-1u)) > 0) {
            found_any=true;
            generation=gen+1u;
            session_id=envlog_read_u32(h+12u)+1u;
            latest_sector=i;
        }
    }
    if (found_any) next_sector=(latest_sector+1u)%ENVLOG_SECTOR_COUNT;
}
static envlog_sample_t take_snapshot(const sensor_snapshot_t *s) {
    envlog_sample_t out={0};
    // Original sensor manager timestamp is sampled at acquisition start.
    if (s->uptime_ms < last_uptime_ms)
        uptime_wrap_ms += (1ull<<32);
    last_uptime_ms=s->uptime_ms;
    out.timestamp_us=(uptime_wrap_ms+s->uptime_ms)*1000u;
    out.sequence=s->sequence;
    if (s->bme280_ok &&
        s->bme280.temperature_centi_c >= -32768 &&
        s->bme280.temperature_centi_c <= 32767 &&
        s->bme280.humidity_centi_percent <= 65535u &&
        s->bme280.pressure_pa <= 0xffffffu) {
        out.flags|=ENVLOG_BME_VALID;
        out.temperature_centi_c=(int16_t)s->bme280.temperature_centi_c;
        out.humidity_centi_rh=(uint16_t)s->bme280.humidity_centi_percent;
        out.pressure_pa=s->bme280.pressure_pa;
    }
    if (s->tsl2591_ok) {
        out.flags|=ENVLOG_TSL_VALID;
        if (s->tsl2591.saturated) out.flags|=ENVLOG_TSL_SATURATED;
        out.tsl_ch0=s->tsl2591.full_count; out.tsl_ch1=s->tsl2591.ir_count;
    }
    if (s->ltr390_ok) {
        out.flags|=ENVLOG_LTR_VALID;
        out.ltr_als=s->ltr390.als_counts; out.ltr_uv=s->ltr390.uvs_counts;
    }
    if (s->sgp40_ok) {
        out.flags|=ENVLOG_SGP_VALID;
        out.sgp_sraw=s->sgp40.sraw_voc;
        if (s->sgp40.index_ready) {
            out.flags|=ENVLOG_VOC_READY;
            out.sgp_voc_index=(uint16_t)s->sgp40.voc_index;
        }
    }
    return out;
}
void envlog_logger_append(const sensor_snapshot_t *s) {
    if (!active || !s) return;
    envlog_sample_t sample=take_snapshot(s);
    // Previous page is pending: never overwrite buffered measurements.
    if (pending_count==ENVLOG_RECORDS_PER_PAGE || flush_partial) {
        ++dropped; return;
    }
    if (pending_count) {
        const envlog_sample_t *first=&pending[0];
        if (sample.sequence != first->sequence+(uint32_t)pending_count ||
            sample.timestamp_us < first->timestamp_us ||
            sample.timestamp_us-first->timestamp_us >= 65535000ull) {
            flush_partial=true;
            ++dropped; // Record the gap; no fabricated timestamp/sequence.
            return;
        }
    }
    pending[pending_count++]=sample;
}
void envlog_logger_poll(void) {
    if (!active) return;
    if (!sector_ready && !select_and_open_sector()) { active=false; return; }
    if (!pending_count ||
        (pending_count<ENVLOG_RECORDS_PER_PAGE && !flush_partial)) return;
    if (!envlog_make_page(flash_page, pending, pending_count)) {
        active=false; return;
    }
    const uint32_t off=sector_offset(active_sector)+page_index*ENVLOG_PAGE_BYTES;
    if (!all_ff(xip(off), ENVLOG_PAGE_BYTES) ||
        !safe_flash(off, flash_page, false)) {
        active=false; return;
    }
    pending_count=0; flush_partial=false;
    ++page_index;
    if (page_index>=16u) sector_ready=false;
}
bool envlog_logger_enabled(void) { return active; }
uint32_t envlog_logger_dropped(void) { return dropped; }
