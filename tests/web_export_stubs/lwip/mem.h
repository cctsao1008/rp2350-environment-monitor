#ifndef TEST_LWIP_MEM_H
#define TEST_LWIP_MEM_H
#include <stddef.h>
void *mem_malloc(size_t size);
void mem_free(void *p);
#endif
