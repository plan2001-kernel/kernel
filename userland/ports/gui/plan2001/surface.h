#ifndef PLAN2001_PUBLIC_SURFACE_H
#define PLAN2001_PUBLIC_SURFACE_H
#include <stdint.h>
#include "obj.h"
/* Available only when the launcher explicitly delegates a session surface.
 * Release the writer mapping before fork/exec; failed operations retain it. */
int p9_surface_handle(void);
uint32_t *p9_surface_map(ObjSurfaceInfo *);
int p9_surface_present(void);
int p9_surface_unmap(void);
#endif
