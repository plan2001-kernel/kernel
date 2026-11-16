#ifndef PLAN2001_TOOLCHAIN_INSTALL_H
#define PLAN2001_TOOLCHAIN_INSTALL_H
#include <stddef.h>
#define P2001_TCC_OID 0x4f45584543000009ULL
#define P2001_TOOLCHAIN_BUNDLE_OID 0x4f4558454300000aULL
/* Trusted bootstrap only: caller supplies its authorized GE/O volume.
 * Installs files into the already delegated Unix root, never opens a volume. */
int p2001_toolchain_install(const void *, size_t, int);
#endif
