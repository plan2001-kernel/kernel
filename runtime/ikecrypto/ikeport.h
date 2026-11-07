#ifndef PLAN2001_IKEPORT_H
#define PLAN2001_IKEPORT_H
/* Pure protocol code shares this explicit-width boundary with host validation.
 * Kernel builds deliberately do not include host/glibc headers. */
#ifdef PLAN2001_GCC
typedef unsigned char uint8_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;
typedef __SIZE_TYPE__ size_t;
#define UINT64_C(x) x##ULL
#define UINT64_MAX 0xffffffffffffffffULL
#define UINT32_MAX 0xffffffffU
#else
#include <stdint.h>
#include <stddef.h>
#endif
_Static_assert(sizeof(uint8_t)==1 && sizeof(uint32_t)==4 && sizeof(uint64_t)==8, "IKE integer widths");
#endif
