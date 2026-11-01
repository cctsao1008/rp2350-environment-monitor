/*
 * Experimental web export: bounded 4 KiB sector snapshots from NOR XIP,
 * serialized on the single lwIP core. No arbitrary-address reads.
 * These endpoints MUST be protected by Cloudflare Access at the edge.
 * ENABLE_BINARY_WEB_EXPORT defaults OFF; do not expose unauthenticated.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hardware/regs/addressmap.h"
#include "lwip/apps/fs.h"
#include "envlog_codec.h"
#include "log_web.h"
#include "log_web_page.h"

#define LOG_HTTP_BUF (ENVLOG_SECTOR_BYTES + 256u)
#define LOG_LIST_STRIDE 64u

static char response[LOG_HTTP_BUF];
static bool busy;

static const char bad_request[] =
    "HTTP/1.0 400 Bad Request\r\n"
    "Content-Type: text/plain\r\nCache-Control: no-store\r\n"
    "Connection: close\r\n\r\nInvalid log query\n";
static const char conflict[] =
    "HTTP/1.0 409 Conflict\r\n"
    "Content-Type: text/plain\r\nCache-Control: no-store\r\n"
    "Connection: close\r\n\r\nLog sector changed; restart download\n";
static const char unavailable[] =
    "HTTP/1.0 503 Service Unavailable\r\n"
    "Content-Type: text/plain\r\nCache-Control: no-store\r\n"
    "Retry-After: 1\r\nConnection: close\r\n\r\nLog export is busy\n";

static void literal(struct fs_file *file, const char *bytes, size_t n) {
    file->data=bytes;
    file->len=(int)n;
    file->index=file->len;
    file->flags=FS_FILE_FLAGS_HEADER_INCLUDED;
    file->pextension=NULL;
}
static void buffered(struct fs_file *file, size_t used) {
    literal(file,response,used);
    file->pextension=(fs_file_extension *)response; // marker, never free()
    busy=true;
}
static bool route(const char *path, const char *target) {
    size_t n=strlen(target);
    return strncmp(path,target,n)==0 && (path[n]=='?' || path[n]==0);
}
/*
 * lwIP's http_find_file() replaces '?' with '\\0' BEFORE calling fs_open()
 * and fs_open_custom(), even with LWIP_HTTPD_CGI=0. Thus the custom file
 * callback cannot see query parameters. Route parameters are encoded as
 * decimal path segments instead, within a fixed 3072-sector address space.
 *
 * Strict parser: no signs, no empty segments, no overflow, no extra fields.
 */
static bool route_family(const char *url, const char *base) {
    const size_t n = strlen(base);
    return strncmp(url,base,n)==0 && (url[n]=='/' || url[n]=='\\0');
}
static bool decimal_segment(const char **cursor,uint32_t *out,bool last) {
    const char *p=*cursor;
    if (*p<'0' || *p>'9') return false;
    uint32_t v=0;
    do {
        const uint32_t digit=(uint32_t)(*p-'0');
        if (v>(UINT32_MAX-digit)/10u) return false;
        v=v*10u+digit;
        ++p;
    } while (*p>='0' && *p<='9');
    if (last) {
        if (*p!='\\0') return false;
    } else {
        if (*p!='/') return false;
        ++p;
    }
    *out=v;
    *cursor=p;
    return true;
}
static bool list_slot(const char *url,uint32_t *start) {
    const char prefix[]="/api/v1/log/list/";
    if (strncmp(url,prefix,sizeof(prefix)-1u)!=0) return false;
    const char *p=url+sizeof(prefix)-1u;
    return decimal_segment(&p,start,true);
}
static bool sector_identity(const char *url,uint32_t *slot,
                            uint32_t *generation,uint32_t *crc) {
    const char prefix[]="/api/v1/log/sector/";
    if (strncmp(url,prefix,sizeof(prefix)-1u)!=0) return false;
    const char *p=url+sizeof(prefix)-1u;
    return decimal_segment(&p,slot,false) &&
           decimal_segment(&p,generation,false) &&
           decimal_segment(&p,crc,true);
}
static const uint8_t *sector_at(uint32_t slot) {
    return (const uint8_t *)(uintptr_t)(
        XIP_BASE + ENVLOG_PARTITION_OFFSET + slot*ENVLOG_SECTOR_BYTES);
}
static bool valid_meta(uint32_t slot) {
    return slot<ENVLOG_SECTOR_COUNT &&
           envlog_sector_header_valid(sector_at(slot));
}
static bool output_header(size_t *n,const char *content_type,uint32_t length) {
    int len=snprintf(response,LOG_HTTP_BUF,
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %lu\r\n"
        "Cache-Control: no-store, no-cache, must-revalidate\r\n"
        "X-Content-Type-Options: nosniff\r\n"
        "Connection: close\r\n\r\n",
        content_type,(unsigned long)length);
    if (len<0 || (size_t)len>LOG_HTTP_BUF-length) return false;
    *n=(size_t)len;
    return true;
}
static bool json_status(struct fs_file *file) {
    unsigned valid=0,unknown=0;
    for (uint32_t i=0;i<ENVLOG_SECTOR_COUNT;++i) {
        const uint8_t *p=sector_at(i);
        if (envlog_sector_header_valid(p)) ++valid;
        else {
            // Only 256-byte metadata is checked: a blank header with an
            // abnormal nonblank payload cannot be diagnosed here.
            bool erased=true;
            for (unsigned j=0;j<ENVLOG_HEADER_BYTES;++j)
                if(p[j]!=0xffu){erased=false;break;}
            if(!erased)++unknown;
        }
    }
    char body[256];
    int l=snprintf(body,sizeof(body),
        "{\"schema\":\"rp2350.envlog.export.v1\",\"sectorBytes\":4096,"
        "\"sectorCount\":3072,\"partitionBytes\":12582912,"
        "\"manifestPage\":64,\"validSectors\":%u,\"unknownHeaders\":%u,"
        "\"version\":1}\n", valid,unknown);
    if(l<0||(size_t)l>=sizeof(body))return false;
    size_t n=0;
    if(!output_header(&n,"application/json",(uint32_t)l))return false;
    memcpy(response+n,body,(size_t)l);
    buffered(file,n+(size_t)l);
    return true;
}
static bool json_list(struct fs_file *file,uint32_t start) {
    if(start>=ENVLOG_SECTOR_COUNT || start%LOG_LIST_STRIDE!=0) return false;
    // A physical-slot manifest is intentionally stable and paginated:
    // the browser builds the old exact 12 MiB raw partition dump.
    // Avoid a 3 KiB stack frame: Pico SDK's default main stack is small.
    // This static buffer is safe because all log responses are serialized.
    static char body[3000];
    size_t used=0;
    int n=snprintf(body,sizeof(body),
        "{\"schema\":\"rp2350.envlog.list.v1\",\"start\":%lu,\"entries\":[",
        (unsigned long)start);
    if(n<0||(size_t)n>=sizeof(body))return false;
    used=(size_t)n;
    bool first=true;
    const uint32_t limit=start+LOG_LIST_STRIDE<ENVLOG_SECTOR_COUNT
        ?start+LOG_LIST_STRIDE:ENVLOG_SECTOR_COUNT;
    for(uint32_t slot=start;slot<limit;++slot) {
        if(!valid_meta(slot))continue;
        const uint8_t *h=sector_at(slot);
        n=snprintf(body+used,sizeof(body)-used,"%s[%lu,%lu,%lu]",
            first?"":",",(unsigned long)slot,
            (unsigned long)envlog_read_u32(h+8),
            (unsigned long)envlog_read_u32(h+252));
        if(n<0||(size_t)n>=sizeof(body)-used)return false;
        used+=(size_t)n;first=false;
    }
    n=snprintf(body+used,sizeof(body)-used,"]}\n");
    if(n<0||(size_t)n>=sizeof(body)-used)return false;
    used+=(size_t)n;
    size_t head=0;
    if(!output_header(&head,"application/json",(uint32_t)used))return false;
    memcpy(response+head,body,used);
    buffered(file,head+used);
    return true;
}
static bool binary_sector(struct fs_file *file,uint32_t slot,
                          uint32_t generation,uint32_t header_crc) {
    if(!valid_meta(slot))return false;
    const uint8_t *flash=sector_at(slot);
    if(envlog_read_u32(flash+8)!=generation ||
       envlog_read_u32(flash+252)!=header_crc) return false;
    size_t prefix=0;
    if(!output_header(&prefix,"application/octet-stream",ENVLOG_SECTOR_BYTES))
        return false;
    // SRAM snapshot avoids torn HTTP responses if the append loop updates
    // later pages or a circular-ring erase occurs during network transport.
    memcpy(response+prefix,flash,ENVLOG_SECTOR_BYTES);
    if(!envlog_sector_header_valid((const uint8_t *)(response+prefix)))
        return false;
    buffered(file,prefix+ENVLOG_SECTOR_BYTES);
    return true;
}
int log_web_open(struct fs_file *file,const char *name) {
    if(!file||!name)return 0;
    if(route(name,"/logs.html")){
        literal(file,log_web_page,sizeof(log_web_page)-1u);
        return 1;
    }
    bool status=route(name,"/api/v1/log/status");
    bool list=route_family(name,"/api/v1/log/list");
    bool sector=route_family(name,"/api/v1/log/sector");
    if(!status&&!list&&!sector)return 0;
    if(busy){
        literal(file,unavailable,sizeof(unavailable)-1u);
        return 1;
    }
    if(status){
        if(!json_status(file))literal(file,unavailable,sizeof(unavailable)-1u);
        return 1;
    }
    if(list){
        uint32_t start=0;
        if(!list_slot(name,&start) || !json_list(file,start))
            literal(file,bad_request,sizeof(bad_request)-1u);
        return 1;
    }
    uint32_t slot=0,generation=0,crc=0;
    if(!sector_identity(name,&slot,&generation,&crc) ||
       slot>=ENVLOG_SECTOR_COUNT){
        literal(file,bad_request,sizeof(bad_request)-1u);
    }else if(!binary_sector(file,slot,generation,crc)){
        literal(file,conflict,sizeof(conflict)-1u);
    }
    return 1;
}
bool log_web_close(struct fs_file *file){
    if(file && (void*)file->pextension==(void*)response){
        busy=false;
        file->pextension=NULL;
        return true;
    }
    return false;
}
