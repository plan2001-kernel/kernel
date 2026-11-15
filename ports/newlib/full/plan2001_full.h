#ifndef PLAN2001_NEWLIB_FULL_H
#define PLAN2001_NEWLIB_FULL_H
#include <stdint.h>
#include <stddef.h>
long p9_full_region_create(uint64_t size);
long p9_full_region_map(int handle, int readonly);
long p9_full_region_unmap(uintptr_t base);
long p9_full_region_close(int handle);
/* Process exit releases each open description once, publishing pipe EOF. */
void p9_full_close_descriptors(void);
/* Explicit six-grant UXPIPE supervisor session; never guesses console rights. */
#include "../../unix/uxservice.h"
int p9_full_bind_files(UxService *service);
void p9_full_unbind_files(void);
/* Returns only an explicitly attached userspace service; never a Handle. */
UxService *p9_full_file_service(void);
int p9_full_bind_uxpipe(const int region_handles[6]);
/* Enumerate and validate six exact Oinheritv stream grants + Vexecute.
 * No implicit console, OID, or arbitrary writable-region fallback. */
int p9_full_bind_inherited_uxpipe(void);
/* Exact supervisor-validated launch grant; 0 for standalone/no grant. */
int p9_full_executable_volume(void);
/* Called by p9_start: 1 bound, 0 standalone, -1 invalid partial grants. */
int p9_full_auto_bind_uxpipe(void);
void p9_full_unbind_uxpipe(void);
int p9_full_bind_stdin(int region_handle);
void p9_full_unbind_stdin(void);
#endif
