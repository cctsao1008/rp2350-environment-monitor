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
#define PBUF_POOL_SIZE 4
#define PBUF_POOL_BUFSIZE 1600
#define HTTPD_USE_CUSTOM_FSDATA 0
#define LWIP_MULTICAST_PING 1
#define LWIP_BROADCAST_PING 1
#endif
