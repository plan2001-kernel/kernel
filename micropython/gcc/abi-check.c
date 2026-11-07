/*
 * N9.6 user ABI static checks.  Built with the same flags as the target
 * objects; not linked into MicroPython.  These catch accidental Plan 9 C
 * assumptions (32-bit long/pointer) leaking into the GCC user build.
 */
#include "p9compat.h"

_Static_assert(sizeof(char) == 1, "char");
_Static_assert(sizeof(short) == 2, "short");
_Static_assert(sizeof(int) == 4, "int");
_Static_assert(sizeof(long) == 8, "GCC LP64 long");
_Static_assert(sizeof(long long) == 8, "long long");
_Static_assert(sizeof(void *) == 8, "pointer");
_Static_assert(sizeof(size_t) == 8, "size_t");
_Static_assert(sizeof(ssize_t) == 8, "ssize_t");
_Static_assert(sizeof(usize) == 8, "Plan 9 usize");
_Static_assert(sizeof(uintptr) == 8, "Plan 9 uintptr");
_Static_assert(sizeof(uvlong) == 8, "Plan 9 uvlong");
_Static_assert(sizeof(jmp_buf) == 8 * sizeof(uintptr),
               "GCC jmp_buf must hold SP/PC/RBX/RBP/R12-R15");

struct AbiProbe {
	char c;
	int i;
	void *p;
	long l;
};
_Static_assert(__builtin_offsetof(struct AbiProbe, c) == 0, "offsetof c");
_Static_assert(__builtin_offsetof(struct AbiProbe, i) == 4, "offsetof i");
_Static_assert(__builtin_offsetof(struct AbiProbe, p) == 8, "offsetof p");
_Static_assert(__builtin_offsetof(struct AbiProbe, l) == 16, "offsetof l");

int
p9_abi_probe(void)
{
	return (int)(sizeof(long) + sizeof(void *) + sizeof(jmp_buf));
}
