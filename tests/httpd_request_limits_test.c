/*
 * Compile twice: Web Export enabled and disabled.
 * Verify the Cloudflare Access header fix does not bloat normal NCM images.
 */
#include "lwipopts.h"

#if PROJECT_ENABLE_BINARY_WEB_EXPORT
_Static_assert(LWIP_HTTPD_SUPPORT_REQUESTLIST == 1, "large requests require requestlist");
_Static_assert(LWIP_HTTPD_MAX_REQ_LENGTH == 8192, "request parser needs 8 KiB");
_Static_assert(LWIP_HTTPD_REQ_BUFSIZE == 8192, "queued request limit needs 8 KiB");
_Static_assert(LWIP_HTTPD_REQ_QUEUELEN >= 8, "must allow segmented headers");
_Static_assert(PBUF_POOL_SIZE >= 8, "pool must allow segmented headers");
#else
#ifdef LWIP_HTTPD_MAX_REQ_LENGTH
#error "Default NCM builds must retain lwIP HTTPD's 1023 B upstream setting"
#endif
#ifdef LWIP_HTTPD_REQ_BUFSIZE
#error "Default NCM builds must retain the upstream HTTPD queue size"
#endif
_Static_assert(PBUF_POOL_SIZE == 4, "default NCM pool must be unchanged");
#endif

_Static_assert(PBUF_POOL_BUFSIZE == 1600, "NCM segment size must be unchanged");

int main(void) { return 0; }
