#ifndef RP2350_LOG_WEB_H
#define RP2350_LOG_WEB_H
#include "lwip/apps/fs.h"
// Returns one if handled. Endpoints must be protected at the Cloudflare edge.
int log_web_open(struct fs_file *file, const char *path);
// True if the file is owned by the singleton fixed-RAM log transfer buffer.
bool log_web_close(struct fs_file *file);
#endif
