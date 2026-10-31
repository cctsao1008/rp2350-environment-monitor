#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "envlog_codec.h"

int main(void) {
    envlog_sample_t s[10]={0};
    for (unsigned i=0; i<10; ++i) {
        s[i].timestamp_us=12345000ull + 1000000ull*i;
        s[i].sequence=100u+i;
        s[i].temperature_centi_c=2451;
        s[i].humidity_centi_rh=6134;
        s[i].pressure_pa=100123;
        s[i].tsl_ch0=2100; s[i].tsl_ch1=340;
        s[i].ltr_als=3180; s[i].ltr_uv=0;
        s[i].sgp_sraw=30050; s[i].sgp_voc_index=95;
        s[i].flags=0x2fu;
    }
    uint8_t page[ENVLOG_PAGE_BYTES];
    uint8_t head[ENVLOG_HEADER_BYTES];
    assert(envlog_make_page(page,s,10));
    assert(envlog_page_valid(page));
    assert(page[16]==0 && page[17]==0);
    assert(page[16+9]==0x34 && page[16+10]==0x08); // CH0 = 2100
    assert(page[16+23]==0x2fu);
    const uint32_t crc=envlog_read_u32(page+12);
    printf("Golden page CRC32: %08lx\n", (unsigned long)crc);
    page[33]^=1u;
    assert(!envlog_page_valid(page));
    page[33]^=1u;
    assert(envlog_make_page(page,s,3));
    assert(envlog_page_valid(page));
    assert(page[16+3*24]==0xffu && page[16+3*24+1]==0xffu);
    s[1].sequence+=2u;
    assert(!envlog_make_page(page,s,10));
    s[1].sequence-=2u;
    s[1].timestamp_us=s[0].timestamp_us+65535000ull;
    assert(!envlog_make_page(page,s,10));
    envlog_make_sector_header(head,0xffffffffu,7u);
    assert(envlog_sector_header_valid(head));
    assert(envlog_read_u32(head+8)==0xffffffffu);
    head[32]^=1u;
    assert(!envlog_sector_header_valid(head));
    puts("envlog codec host tests PASS");
    return 0;
}
