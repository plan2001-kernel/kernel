#ifndef PLAN2001_SURFACE_H
#define PLAN2001_SURFACE_H
#include <stdint.h>
#include "obj.h"
#include "process.h"
int p9_surface_handle(void);
int p9_surface_bind(int);
int p9_surface_export(P9Plan *,int);
uint32_t *p9_surface_map(ObjSurfaceInfo *);
int p9_surface_present(void);
int p9_surface_unmap(void);
#endif
