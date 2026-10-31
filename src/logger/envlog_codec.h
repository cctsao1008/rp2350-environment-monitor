#ifndef RP2350_ENVLOG_CODEC_H
#define RP2350_ENVLOG_CODEC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Binary protocol v1: explicit little endian, no native C struct packing.
#define ENVLOG_RECORD_BYTES 24u
#define ENVLOG_PAGE_BYTES 256u
#define ENVLOG_SECTOR_BYTES 4096u
#define ENVLOG_HEADER_BYTES 256u
#define ENVLOG_RECORDS_PER_PAGE 10u
#define ENVLOG_PAGES_PER_SECTOR 15u
#define ENVLOG_FORMAT_VERSION 1u
#define ENVLOG_PARTITION_OFFSET (4u * 1024u * 1024u)
#define ENVLOG_PARTITION_BYTES (12u * 1024u * 1024u)
#define ENVLOG_SECTOR_COUNT (ENVLOG_PARTITION_BYTES / ENVLOG_SECTOR_BYTES)

// Per-sample flags: never substitute zero for a failed sensor sample.
#define ENVLOG_BME_VALID      0x01u
#define ENVLOG_TSL_VALID      0x02u
#define ENVLOG_LTR_VALID      0x04u
#define ENVLOG_SGP_VALID      0x08u
#define ENVLOG_TSL_SATURATED  0x10u
#define ENVLOG_VOC_READY      0x20u

typedef struct {
    uint64_t timestamp_us; // monotonic sample-start time, not wall-clock UTC
    uint32_t sequence;
    int16_t temperature_centi_c;
    uint16_t humidity_centi_rh;
    uint32_t pressure_pa;
    uint16_t tsl_ch0, tsl_ch1;
    uint32_t ltr_als, ltr_uv;
    uint16_t sgp_sraw, sgp_voc_index;
    uint8_t flags;
} envlog_sample_t;

// 0..11 page header: uint64 base_us, uint32 base_sequence.
// 12..15 CRC32 over bytes 0..11 followed by 16..255.
// 16..255: ten 24-byte records, trailing unused records remain 0xff.
// At record byte 0, 0xffff means unused slot (valid offsets <= 65534).
uint32_t envlog_crc32(const uint8_t *bytes, size_t length);
bool envlog_make_page(uint8_t out[ENVLOG_PAGE_BYTES],
                      const envlog_sample_t *samples, size_t count);
bool envlog_page_valid(const uint8_t page[ENVLOG_PAGE_BYTES]);

// Sector header 0..255: magic 'ELOG', version, generation, session_id,
// record configuration and CRC32 at 252..255. Reserved bytes are 0xff.
void envlog_make_sector_header(uint8_t out[ENVLOG_HEADER_BYTES],
                               uint32_t generation, uint32_t session_id);
bool envlog_sector_header_valid(const uint8_t head[ENVLOG_HEADER_BYTES]);
uint32_t envlog_read_u32(const uint8_t *p);
#endif
