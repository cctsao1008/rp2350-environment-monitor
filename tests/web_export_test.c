#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "envlog_codec.h"
#include "log_web.h"

uint8_t test_nor_flash[ENVLOG_PARTITION_BYTES];

static const char *body(const struct fs_file *f) {
    const char *p=strstr(f->data,"\r\n\r\n");
    return p?p+4:NULL;
}
static void close_file(struct fs_file *f) {
    (void)log_web_close(f);
}
int main(void) {
    memset(test_nor_flash,0xff,sizeof(test_nor_flash));
    uint8_t sector_head[256];
    envlog_make_sector_header(sector_head,17u,3u);
    memcpy(test_nor_flash,sector_head,256u);
    struct fs_file f={0},g={0};
    assert(log_web_open(&f,"/api/v1/log/status")==1);
    assert(strstr(f.data,"200 OK"));
    assert(strstr(body(&f),"\"validSectors\":1"));
    close_file(&f);

    assert(log_web_open(&f,"/api/v1/log/list?start=0")==1);
    assert(strstr(body(&f),"[0,17,"));
    close_file(&f);

    uint32_t crc=envlog_read_u32(sector_head+252);
    char url[180];
    snprintf(url,sizeof(url),
             "/api/v1/log/sector?slot=0&gen=17&crc=%lu",
             (unsigned long)crc);
    assert(log_web_open(&f,url)==1);
    assert(strstr(f.data,"200 OK"));
    assert(f.len>4096);
    const char *bytes=body(&f);
    assert(bytes && memcmp(bytes,sector_head,256)==0);
    // A second concurrent request must not overwrite the first buffer.
    assert(log_web_open(&g,url)==1);
    assert(strstr(g.data,"503 Service Unavailable"));
    assert(memcmp(bytes,sector_head,256)==0);
    close_file(&g);
    close_file(&f);
    // Busy lock must be released only when first response closes.
    assert(log_web_open(&f,url)==1);
    assert(strstr(f.data,"200 OK"));
    close_file(&f);

    assert(log_web_open(&f,"/api/v1/log/sector?slot=3072&gen=17&crc=5")==1);
    assert(strstr(f.data,"400 Bad Request"));
    assert(log_web_open(&f,"/api/v1/log/sector?slot=0&gen=18&crc=5")==1);
    assert(strstr(f.data,"409 Conflict"));
    assert(log_web_open(&f,"/api/v1/log/sector?slot=0&gen=17&crc=1")==1);
    assert(strstr(f.data,"409 Conflict"));
    assert(log_web_open(&f,"/api/v1/log/sector?slot=0&gen=17&crc=1&slot=0")==1);
    assert(strstr(f.data,"400 Bad Request"));
    assert(log_web_open(&f,"/api/v1/log/list?start=63")==1);
    assert(strstr(f.data,"400 Bad Request"));
    assert(log_web_open(&f,"/api/v1/log/list?start=4294967296")==1);
    assert(strstr(f.data,"400 Bad Request"));
    assert(log_web_open(&f,"/api/v1/log/list?start=64")==1);
    assert(strstr(f.data,"200 OK"));
    close_file(&f);
    assert(log_web_open(&f,"/logs.html")==1);
    assert(strstr(f.data,"Download Binary Log"));
    close_file(&f);
    assert(log_web_open(&f,"/api/v1/sensors.json")==0);
    puts("web log export host tests PASS");
    return 0;
}
