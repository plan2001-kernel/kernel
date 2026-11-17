/* One explicitly delegated session surface; no ambient framebuffer access. */
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include "surface.h"
#include "plan2001_full.h"
#include "vobj.h"
extern long objinvoke(int,void *,long);
static int surface;
static uintptr_t mapped;
static ObjSurfaceInfo geometry;
int p9_surface_handle(void){return surface;}
int p9_surface_bind(int handle)
{
    ObjSurfaceInfo info;ObjCall c={0};
    if(handle<=0){if(mapped && p9_surface_unmap())return -1;surface=0;mapped=0;memset(&geometry,0,sizeof geometry);return 0;}
    c.version=OBJ_ABI_VERSION;c.op=ObjOpSurfaceInfo;c.out=&info;c.outlen=sizeof info;
    if(objinvoke(handle,&c,sizeof c)<0 || info.version!=OBJ_SURFACE_VERSION ||
       info.format!=OBJ_SURFACE_XRGB8888 || !info.width || !info.height ||
       info.stride!=info.width || info.pixel_bytes!=(uint64_t)info.width*info.height*4 ||
       info.pixel_bytes>64u*1024u*1024u){errno=EACCES;return -1;}
    if(mapped && p9_surface_unmap())return -1;
    surface=handle;geometry=info;mapped=0;return 0;
}
int p9_surface_unmap(void)
{
    if(!mapped)return 0;
    if(p9_full_region_unmap(mapped)<0){errno=EIO;return -1;}
    mapped=0;return 0;
}
int p9_surface_export(P9Plan *plan,int exec)
{
    if(!surface)return -1;
    (void)exec;
    /* A failed fork/exec must leave the caller's mapping intact. */
    if(mapped){errno=EBUSY;return -2;}
    int index=p9_plan_add(plan,surface,Vread|Vwrite|Vmeta|Vdelegate);
    return index<0?-2:index;
}
uint32_t *p9_surface_map(ObjSurfaceInfo *out)
{
    if(!surface){errno=EACCES;return NULL;}
    if(!mapped){long p=p9_full_region_map(surface,0);if(p<=0){errno=EACCES;return NULL;}mapped=(uintptr_t)p;}
    if(out)*out=geometry;return (uint32_t *)mapped;
}
int p9_surface_present(void)
{
    if(!surface || !mapped){errno=EACCES;return -1;}
    ObjCall c={0};c.version=OBJ_ABI_VERSION;c.op=ObjOpSurfacePresent;
    if(objinvoke(surface,&c,sizeof c)<0){errno=EACCES;return -1;}
    return 0;
}
