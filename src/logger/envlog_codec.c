#include "envlog_codec.h"
#include <string.h>

static void put16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
}
static void put24(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16);
}
static void put32(uint8_t *p, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (8u*i));
}
static void put64(uint8_t *p, uint64_t v) {
    for (unsigned i = 0; i < 8; ++i) p[i] = (uint8_t)(v >> (8u*i));
}
static uint16_t get16(const uint8_t *p) {
    return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8);
}
uint32_t envlog_read_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
uint32_t envlog_crc32(const uint8_t *p, size_t n) {
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < n; ++i) {
        crc ^= p[i];
        for (unsigned j = 0; j < 8; ++j)
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u);
    }
    return ~crc;
}
static uint32_t page_crc(const uint8_t page[ENVLOG_PAGE_BYTES]) {
    // CRC32/ISO-HDLC over (page[0..11] || page[16..255]).
    // Streaming implementation avoids a second 252-byte buffer.
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < ENVLOG_PAGE_BYTES; ++i) {
        if (i >= 12u && i < 16u) continue;
        crc ^= page[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u);
    }
    return ~crc;
}
bool envlog_make_page(uint8_t out[ENVLOG_PAGE_BYTES],
                      const envlog_sample_t *s, size_t count) {
    if (!out || !s || !count || count > ENVLOG_RECORDS_PER_PAGE) return false;
    const uint64_t base = s[0].timestamp_us;
    const uint32_t base_seq = s[0].sequence;
    // Validate *before* writing, to avoid a partially prepared output.
    uint16_t offsets[ENVLOG_RECORDS_PER_PAGE];
    for (size_t i = 0; i < count; ++i) {
        if (s[i].sequence != base_seq + (uint32_t)i ||
            s[i].timestamp_us < base ||
            (s[i].timestamp_us - base) / 1000u > 65534u ||
            s[i].pressure_pa > 0xffffffu ||
            s[i].ltr_als > 0xffffffu || s[i].ltr_uv > 0xffffffu)
            return false;
        offsets[i] = (uint16_t)((s[i].timestamp_us - base) / 1000u);
        if (i && offsets[i] < offsets[i-1]) return false;
    }
    memset(out, 0xff, ENVLOG_PAGE_BYTES);
    put64(out, base);
    put32(out + 8, base_seq);
    for (size_t i = 0; i < count; ++i) {
        uint8_t *p = out + 16u + i * ENVLOG_RECORD_BYTES;
        put16(p, offsets[i]);
        put16(p + 2, (uint16_t)s[i].temperature_centi_c);
        put16(p + 4, s[i].humidity_centi_rh);
        put24(p + 6, s[i].pressure_pa);
        put16(p + 9, s[i].tsl_ch0); put16(p + 11, s[i].tsl_ch1);
        put24(p + 13, s[i].ltr_als); put24(p + 16, s[i].ltr_uv);
        put16(p + 19, s[i].sgp_sraw);
        put16(p + 21, s[i].sgp_voc_index);
        p[23] = s[i].flags;
    }
    put32(out + 12, page_crc(out));
    return true;
}
bool envlog_page_valid(const uint8_t page[ENVLOG_PAGE_BYTES]) {
    if (!page || envlog_read_u32(page + 12) != page_crc(page)) return false;
    // The first slot must be a real zero-offset sample.
    if (get16(page + 16) != 0u) return false;
    bool found_unused = false;
    for (size_t i=0; i<ENVLOG_RECORDS_PER_PAGE; ++i) {
        const uint16_t offset = get16(page + 16u + i*ENVLOG_RECORD_BYTES);
        if (offset == 0xffffu) {
            found_unused = true;
        } else if (found_unused || (i && offset < get16(page + 16u +
                                  (i-1u)*ENVLOG_RECORD_BYTES))) {
            return false;
        }
    }
    return true;
}
void envlog_make_sector_header(uint8_t out[ENVLOG_HEADER_BYTES],
                               uint32_t generation, uint32_t session_id) {
    memset(out, 0xff, ENVLOG_HEADER_BYTES);
    memcpy(out, "ELOG", 4);
    put16(out + 4, ENVLOG_FORMAT_VERSION);
    put16(out + 6, ENVLOG_HEADER_BYTES);
    put32(out + 8, generation);
    put32(out + 12, session_id);
    put32(out + 16, ENVLOG_PARTITION_OFFSET);
    put32(out + 20, ENVLOG_PARTITION_BYTES);
    put16(out + 24, ENVLOG_RECORD_BYTES);
    put16(out + 26, ENVLOG_PAGE_BYTES);
    put16(out + 28, ENVLOG_SECTOR_BYTES);
    put16(out + 30, ENVLOG_RECORDS_PER_PAGE);
    // Current firmware settings (immutable within one session).
    put16(out + 32, 408u);  // TSL2591 device factor
    put16(out + 34, 2700u); // TSL2591 temporary relative factor x1000
    out[36] = 0x10u; // TSL2591 control: 100 ms / 25x
    out[37] = 0x22u; // LTR390 rate: 18 bit / 100 ms
    out[38] = 0x01u; // LTR390 3x
    out[39] = 0u;    // UTC anchor valid: false (no trusted wall clock)
    // Future config/UTC anchor metadata reserved at 40..251 (0xff).
    put32(out + 252, envlog_crc32(out, 252u));
}
bool envlog_sector_header_valid(const uint8_t h[ENVLOG_HEADER_BYTES]) {
    return h && memcmp(h, "ELOG", 4) == 0 &&
           get16(h + 4) == ENVLOG_FORMAT_VERSION &&
           get16(h + 6) == ENVLOG_HEADER_BYTES &&
           envlog_read_u32(h + 16) == ENVLOG_PARTITION_OFFSET &&
           envlog_read_u32(h + 20) == ENVLOG_PARTITION_BYTES &&
           get16(h + 24) == ENVLOG_RECORD_BYTES &&
           get16(h + 26) == ENVLOG_PAGE_BYTES &&
           get16(h + 28) == ENVLOG_SECTOR_BYTES &&
           get16(h + 30) == ENVLOG_RECORDS_PER_PAGE &&
           envlog_read_u32(h + 252) == envlog_crc32(h, 252u);
}
