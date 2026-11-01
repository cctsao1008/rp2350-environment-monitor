#ifndef RP2350_SENSOR_LWIPOPTS_H
#define RP2350_SENSOR_LWIPOPTS_H
// Single-threaded lwIP configuration based on pinned TinyUSB NCM example.
#define NO_SYS 1
#define MEM_ALIGNMENT 4
// Heap also holds one bounded JSON response per active HTTP request.
#define MEM_SIZE 8192
#define LWIP_RAW 0
#define LWIP_NETCONN 0
#define LWIP_SOCKET 0
#define LWIP_DHCP 0
#define LWIP_ICMP 1
#define LWIP_UDP 1
#if PROJECT_ENABLE_SNTP_TIME
/* RP2350 runs its own RFC4330 SNTPv4 client over USB-NCM L3 routing. */
#include <stdint.h>
void rp2350_sntp_apply(uint32_t sec, uint32_t usec);
void rp2350_sntp_request_nonce(int32_t *sec, uint32_t *frac);
#define LWIP_DNS 1
#define DNS_TABLE_SIZE 4
#define SNTP_SERVER_DNS 1
#define SNTP_MAX_SERVERS 1
#define SNTP_CHECK_RESPONSE 2
#define SNTP_COMP_ROUNDTRIP 0
#define SNTP_UPDATE_DELAY 3600000
#define SNTP_RECV_TIMEOUT 15000
#define SNTP_GET_SYSTEM_TIME_NTP(sec, frac) rp2350_sntp_request_nonce(&(sec), &(frac))
#define SNTP_SET_SYSTEM_TIME_US(sec, usec) rp2350_sntp_apply((uint32_t)(sec),(uint32_t)(usec))
/* Include additional SNTP and DNS timer entries. Bounded static pools. */
#define MEMP_NUM_SYS_TIMEOUT 16
#endif
#define LWIP_TCP 1
#define LWIP_IPV4 1
#define LWIP_IPV6 0
#define ETH_PAD_SIZE 0
#define LWIP_IP_ACCEPT_UDP_PORT(p) ((p)==PP_NTOHS(67))
#define TCP_MSS (1500 - 20 - 20)
#define TCP_SND_BUF (4 * TCP_MSS)
#define TCP_WND (4 * TCP_MSS)
#define ETHARP_SUPPORT_STATIC_ENTRIES 1
#define LWIP_HTTPD_CGI 0
#define LWIP_HTTPD_SSI 0
// Phase 2 serves HTML and snapshot JSON through per-request custom files.
#define LWIP_HTTPD_CUSTOM_FILES 1
#define LWIP_HTTPD_FILE_EXTENSION 1
#define LWIP_HTTPD_DYNAMIC_HEADERS 0
#define LWIP_HTTPD_SSI_INCLUDE_TAG 0
#define LWIP_SINGLE_NETIF 1

/*
 * Cloudflare Access forwards authentication cookies/JWT headers to the
 * origin. The lwIP HTTPD default request parsing limit is only 1023 B;
 * requests spanning several NCM TCP segments then fail before fs_open_custom.
 *
 * The native Target tests show 2x512-byte synthetic auth headers pass but
 * 2x1500- and 2x3000-byte headers fail with an empty reply. Use one bounded
 * 8 KiB parser only when the protected Web Binary Export is opted in.
 * LWIP_HTTPD_REQ_BUFSIZE alone is NOT enough: httpd.c copies at most
 * LWIP_HTTPD_MAX_REQ_LENGTH into its shared request parsing buffer.
 *
 * RX pbuf pool/queue sizes also need to accept a multi-segment 8 KiB header.
 * No dynamic unbounded allocation; reserve at most eight 1600-B pbufs.
 */
#if PROJECT_ENABLE_BINARY_WEB_EXPORT
#define LWIP_HTTPD_SUPPORT_REQUESTLIST 1
#define LWIP_HTTPD_REQ_QUEUELEN 8
#define LWIP_HTTPD_REQ_BUFSIZE 8192
#define LWIP_HTTPD_MAX_REQ_LENGTH 8192
#define PBUF_POOL_SIZE 8
#else
#define PBUF_POOL_SIZE 4
#endif
#define PBUF_POOL_BUFSIZE 1600
#define HTTPD_USE_CUSTOM_FSDATA 0
#define LWIP_MULTICAST_PING 1
#define LWIP_BROADCAST_PING 1
#endif
