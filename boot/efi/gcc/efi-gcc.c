/*
 * N9.4 Linux-native UEFI loader support for the Microsoft x64 ABI, reused
 * for the experimental AArch64 loader's AAPCS64 ABI.
 *
 * clang --target=x86_64-pc-windows-msvc (or aarch64-pc-windows-msvc) gives
 * COFF objects and the calling convention UEFI requires.  The existing
 * loader C sources are reused unchanged except for this bounded bridge.
 */
#include <u.h>
#include "fns.h"
#include "efi.h"

typedef UINT64 (*EfiFn)(UINT64, UINT64, UINT64, UINT64, UINT64, UINT64,
			UINT64, UINT64, UINT64, UINT64, UINT64, UINT64, UINT64);

/*
 * Bound the inherited variadic eficall() to 12 arguments.  The first UEFI
 * argument is always the protocol/interface pointer (This); unused trailing
 * arguments are zero and are ignored by firmware functions.
 */
uintptr
eficall_n(void *proc, int n, ...)
{
	UINT64 a[12];
	va_list ap;
	int i;

	for(i = 0; i < 12; i++)
		a[i] = 0;
	va_start(ap, n);
	for(i = 0; i < n && i < 12; i++)
		a[i] = va_arg(ap, UINT64);
	va_end(ap);
	return ((EfiFn)proc)(a[0], a[1], a[2], a[3], a[4], a[5],
		a[6], a[7], a[8], a[9], a[10], a[11], 0);
}

/* PE/COFF relocations are applied by firmware; callbacks need no rebasing. */
void*
rebase(void *addr)
{
	return addr;
}
