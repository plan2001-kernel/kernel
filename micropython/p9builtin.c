/* gcc's bit builtins for MicroPython's misc.h (mp_clz and friends) */
#include "p9compat.h"

int
__builtin_clzll(unsigned long long x)
{
	int n;

	if(x == 0)
		return 64;
	for(n = 0; (x & (1ULL<<63)) == 0; n++)
		x <<= 1;
	return n;
}

int
__builtin_clz(unsigned int x)
{
	return __builtin_clzll(x) - 32;
}

int
__builtin_clzl(unsigned long x)
{
	return __builtin_clzll(x) - 64 + 8*sizeof(unsigned long);
}

int
__builtin_ctz(unsigned int x)
{
	int n;

	if(x == 0)
		return 32;
	for(n = 0; (x & 1) == 0; n++)
		x >>= 1;
	return n;
}
