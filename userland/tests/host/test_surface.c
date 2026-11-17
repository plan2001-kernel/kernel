#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "surface.h"
#include "vobj.h"
static uint32_t pixels[16];
static int maps,unmaps,presents,invalid;
long objinvoke(int h,void *value,long size)
{
    ObjCall *c=value;assert(size==sizeof *c);
    if(h!=42)return -1;
    if(c->op==ObjOpSurfaceInfo){ObjSurfaceInfo i={1,5,4,4,4,OBJ_SURFACE_XRGB8888,64};
        if(invalid)i.width=4096;memcpy(c->out,&i,sizeof i);return 0;}
    if(c->op==ObjOpSurfacePresent){presents++;return 0;}
    return -1;
}
long p9_full_region_map(int h,int ro){assert(h==42 && !ro);maps++;return (long)(uintptr_t)pixels;}
long p9_full_region_unmap(uintptr_t p){assert(p==(uintptr_t)pixels);unmaps++;return 0;}
int p9_plan_add(P9Plan *p,int h,uint64_t rights)
{assert(h==42 && rights==(Vread|Vwrite|Vmeta|Vdelegate));p->count++;return 0;}
int main(void)
{
    ObjSurfaceInfo info;P9Plan plan={0};
    assert(p9_surface_map(&info)==NULL && errno==EACCES);
    assert(p9_surface_bind(41)<0 && p9_surface_handle()==0);
    invalid=1;assert(p9_surface_bind(42)<0 && p9_surface_handle()==0);invalid=0;
    assert(p9_surface_bind(42)==0 && p9_surface_map(&info)==pixels && info.slot==5);
    assert(p9_surface_present()==0 && presents==1 && maps==1);
    assert(p9_surface_export(&plan,0)==-2 && errno==EBUSY);
    assert(p9_surface_export(&plan,1)==-2 && unmaps==0);
    assert(p9_surface_map(NULL)==pixels && maps==1);
    assert(p9_surface_unmap()==0 && unmaps==1);
    assert(p9_surface_export(&plan,0)==0 && plan.count==1);
    assert(p9_surface_map(NULL)==pixels && maps==2);
    assert(p9_surface_bind(0)==0 && unmaps==2 && p9_surface_handle()==0);
    puts("PASS: explicit surface authority, immutable geometry and failed fork/exec mapping preservation");
    return 0;
}
