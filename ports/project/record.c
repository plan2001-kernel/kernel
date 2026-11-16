#include "record.h"
static uint64_t get(const unsigned char *p,unsigned n)
{
    uint64_t v=0;while(n--)v=(v<<8)|*p++;return v;
}
static uint64_t checksum(const unsigned char *p,size_t n)
{
    uint64_t h=UINT64_C(14695981039346656037);
    while(n--){h^=*p++;h*=UINT64_C(1099511628211);}return h;
}
static int entry(const unsigned char *p,size_t n,ProjectEntry *e,size_t *used)
{
    if(n<32)return -1;
    e->binding=get(p,8);e->oid=get(p+8,8);e->rights=(uint32_t)get(p+16,4);
    e->relation=(uint32_t)get(p+20,4);e->kind=(uint32_t)get(p+24,4);
    e->name_length=(size_t)get(p+28,4);
    if(!e->binding || !e->oid || !e->rights || !e->name_length ||
       e->name_length>255 || e->name_length>n-32)return -1;
    e->name=p+32;
    for(size_t i=0;i<e->name_length;i++)if(!e->name[i])return -1;
    *used=32+e->name_length;return 0;
}
int project_record_open(ProjectRecord *r,const void *data,size_t n)
{
    const unsigned char *p=data;ProjectRecord out;ProjectEntry e;size_t off=32,used;
    uint64_t previous=0;
    if(!r || !p || n<40)return -1;
    static const unsigned char magic[8]={'P','2','P','R','O','J','0','1'};
    for(unsigned i=0;i<8;i++)if(p[i]!=magic[i])return -1;
    if(get(p+n-8,8)!=checksum(p,n-8))return -1;
    out.epoch=get(p+8,8);out.next_binding=get(p+16,8);
    uint64_t count=get(p+24,8);
    if(!out.epoch || count>(n-40)/33)return -1;
    out.count=(size_t)count;
    for(size_t i=0;i<out.count;i++){
        if(entry(p+off,n-8-off,&e,&used) || e.binding<=previous ||
           e.binding>out.next_binding)return -1;
        previous=e.binding;off+=used;
    }
    if(off!=n-8)return -1;
    out.records=p+32;out.records_length=n-40;*r=out;return 0;
}
int project_record_entry(const ProjectRecord *r,size_t index,ProjectEntry *e)
{
    size_t off=0,used;
    if(!r || !e || index>=r->count)return -1;
    for(size_t i=0;i<=index;i++){
        if(entry(r->records+off,r->records_length-off,e,&used))return -1;
        off+=used;
    }
    return 0;
}
