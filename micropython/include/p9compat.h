/*
 * ISO C for MicroPython on Plan 9's own libc (6c -p, no APE).
 * Every shim header in this directory includes only this file.
 */
#ifndef P9COMPAT_H
#define P9COMPAT_H
#include <u.h>
#include <libc.h>

/* libc.h's assert is a statement that ignores NDEBUG */
#undef assert
#ifdef NDEBUG
#define assert(x) ((void)0)
#else
#define assert(x) ((x) ? (void)0 : _assert(#x))
#endif
#undef offsetof
#define offsetof(s, m) ((usize)&(((s*)0)->m))

#define NULL nil
typedef usize size_t;
typedef intptr ssize_t;
typedef intptr ptrdiff_t;
typedef s8int int8_t;
typedef u8int uint8_t;
typedef s16int int16_t;
typedef u16int uint16_t;
typedef s32int int32_t;
typedef u32int uint32_t;
typedef s64int int64_t;
typedef u64int uint64_t;
typedef intptr intptr_t;
typedef uintptr uintptr_t;
typedef vlong intmax_t;
typedef uvlong uintmax_t;

#define bool int
#define true 1
#define false 0

#define CHAR_BIT 8
#define SCHAR_MIN (-128)
#define SCHAR_MAX 127
#define UCHAR_MAX 255
#define CHAR_MIN SCHAR_MIN
#define CHAR_MAX SCHAR_MAX
#define SHRT_MIN (-32768)
#define SHRT_MAX 32767
#define USHRT_MAX 65535
#define INT_MIN (-2147483647-1)
#define INT_MAX 2147483647
#define UINT_MAX 4294967295U
#define LONG_MIN INT_MIN
#define LONG_MAX INT_MAX
#define ULONG_MAX 4294967295UL
#define LLONG_MIN (-9223372036854775807LL-1)
#define LLONG_MAX 9223372036854775807LL
#define ULLONG_MAX 18446744073709551615ULL
#define INT8_MIN (-128)
#define INT8_MAX 127
#define UINT8_MAX 255
#define INT16_MIN (-32768)
#define INT16_MAX 32767
#define UINT16_MAX 65535
#define INT32_MIN INT_MIN
#define INT32_MAX INT_MAX
#define UINT32_MAX UINT_MAX
#define INT64_MIN LLONG_MIN
#define INT64_MAX LLONG_MAX
#define UINT64_MAX ULLONG_MAX
#define INTPTR_MIN LLONG_MIN
#define INTPTR_MAX LLONG_MAX
#define UINTPTR_MAX ULLONG_MAX
#define SIZE_MAX ULLONG_MAX
#define SSIZE_MAX LLONG_MAX
#define PTRDIFF_MAX LLONG_MAX
#define INT8_C(c) c
#define INT16_C(c) c
#define INT32_C(c) c
#define INT64_C(c) c##LL
#define UINT8_C(c) c
#define UINT16_C(c) c
#define UINT32_C(c) c##U
#define UINT64_C(c) c##ULL

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

/* gcc builtins MicroPython's misc.h wants (p9builtin.c) */
#define __has_builtin(x) 0
int __builtin_clz(unsigned int);
int __builtin_clzl(unsigned long);
int __builtin_clzll(unsigned long long);
int __builtin_ctz(unsigned int);

/* only debugging code prints with stdio */
#define printf print
#endif
