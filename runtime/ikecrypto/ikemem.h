#ifndef PLAN2001_IKEMEM_H
#define PLAN2001_IKEMEM_H
#include "ikeport.h"
#ifdef PLAN2001_GCC
extern void *memcpy(void *, const void *, size_t);
extern void *memmove(void *, const void *, size_t);
extern void *memset(void *, int, size_t);
extern int memcmp(const void *, const void *, size_t);
#else
#include <string.h>
#endif
#endif
