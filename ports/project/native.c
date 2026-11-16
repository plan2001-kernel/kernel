#include "native.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#ifndef ENOTUNIQ
#define ENOTUNIQ EINVAL
#endif
extern long objinvoke(int,void*,long);
extern long objclose(int);
extern long errstr(char*,unsigned long);
extern unsigned long long _nsec(void);
static void release(void *p){int saved=errno;free(p);errno=saved;}
static int failure(void)
{
    char e[128]={0},restore[128];errstr(e,sizeof e);memcpy(restore,e,sizeof e);errstr(restore,sizeof restore);
    errno=strstr(e,"not found")?ENOENT:strstr(e,"ambiguous")?ENOTUNIQ:
          strstr(e,"exists")?EEXIST:strstr(e,"not empty")?ENOTEMPTY:strstr(e,"epoch changed")?EAGAIN:strstr(e,"cross volume")?EXDEV:
          strstr(e,"file system full")?ENOSPC:strstr(e,"permission")?EACCES: EIO;
    return -1;
}
int pj_call(int h,ObjProjectReq *r,void *out,size_t n)
{
    ObjCall c={0};c.version=OBJ_ABI_VERSION;c.op=ObjOpProject;
    c.in=r;c.inlen=sizeof *r;c.out=out;c.outlen=n;
    if(objinvoke(h,&c,sizeof c)<0)return failure();
    return (int)c.result;
}
int pj_from_handle(PjObject *o,int h)
{
    ObjCall c={0};VobjInfo info={0};c.version=OBJ_ABI_VERSION;c.op=ObjOpInfo;c.out=&info;c.outlen=sizeof info;
    if(objinvoke(h,&c,sizeof c)<0)return failure();
    o->handle=h;o->kind=info.kind;o->rights=info.rights;o->id.oid=info.oid;
    memcpy(o->id.volume,info.volume,16);return 0;
}
void pj_close(PjObject *o){int saved=errno;if(o->handle>0)objclose(o->handle);o->handle=0;errno=saved;}
int pj_blob(PjObject *o,unsigned char **buf,size_t *n,size_t *raw,size_t *meta)
{
    ObjProjectReq r={0};r.operation=ProjectRead;
    int len=pj_call(o->handle,&r,NULL,0);if(len<0)return -1;
    unsigned char *p=malloc((size_t)len);if(!p){errno=ENOMEM;return -1;}
    ObjCall c={0};c.version=OBJ_ABI_VERSION;c.op=ObjOpProject;c.in=&r;c.inlen=sizeof r;c.out=p;c.outlen=(size_t)len;
    if(objinvoke(o->handle,&c,sizeof c)<0 || c.result!=len){release(p);return failure();}
    o->version=c.aux;
    unsigned a,b;if(uxobject_split(p,(unsigned)len,&a,&b)){release(p);errno=EIO;return -1;}
    *buf=p;*n=(size_t)len;*raw=a;*meta=b;return 0;
}
static void put(unsigned char*p,uint64_t v,unsigned n){while(n--){p[n]=(unsigned char)v;v>>=8;}}
static uint64_t hash(const unsigned char*p,size_t n){uint64_t v=UINT64_C(14695981039346656037);while(n--){v^=*p++;v*=UINT64_C(1099511628211);}return v;}
static int framed(const void *raw,size_t n,const UxMeta *m,const void *old,unsigned oldn,unsigned char **out,size_t *total)
{
    if(n>64u*1024u*1024u-UXMETA_ARENA-32){errno=EFBIG;return -1;}
    unsigned char *p=malloc(n+UXMETA_ARENA+32);if(!p){errno=ENOMEM;return -1;}
    unsigned meta;
    if(uxobject_metadata(m,old,oldn,p+n,UXMETA_ARENA,&meta)||uxobject_footer(p+n+meta,n,meta)){release(p);errno=EIO;return -1;}
    if(n)memcpy(p,raw,n);*out=p;*total=n+meta+32;return 0;
}
int pj_empty(const UxMeta *meta,unsigned char **out,size_t *n)
{
    unsigned char raw[40]={0};memcpy(raw,"P2PROJ01",8);put(raw+8,1,8);put(raw+32,hash(raw,32),8);
    return framed(raw,sizeof raw,meta,NULL,0,out,n);
}
static int name(ObjProjectReq *r,const char *s)
{
    size_t n=strlen(s);if(!n || n>255 || strchr(s,'/')){errno=ENAMETOOLONG;return -1;}
    memcpy(r->name,s,n+1);return 0;
}
static int native_resolve(PjObject *parent,const char *label,PjObject *o)
{
    ObjProjectReq r={0};r.operation=ProjectResolve;if(name(&r,label))return -1;
    int h=pj_call(parent->handle,&r,NULL,0);if(h<0)return -1;
    if(pj_from_handle(o,h)){objclose(h);return -1;}return 0;
}
typedef struct ViewWalk {ProjectId id;struct ViewWalk *previous;} ViewWalk;
static int sameid(const ProjectId *a,const ProjectId *b){return a->oid==b->oid && !memcmp(a->volume,b->volume,16);}
static int viewinfo(PjObject *o,int *mode,long long *writer)
{
    unsigned char *p;size_t n,raw,meta;char value[32];
    if(pj_blob(o,&p,&n,&raw,&meta))return -1;
    int rc=uxobject_text_field(p+raw,(unsigned)meta,"project_view",value,sizeof value);
    *writer=-1;*mode=0;
    if(rc>0){*mode=!strcmp(value,"union")?1:!strcmp(value,"intersection")?2:0;
        if(!*mode){release(p);errno=EIO;return -1;}
        rc=uxobject_int_field(p+raw,(unsigned)meta,"write_source",writer);if(rc<0){release(p);errno=EIO;return -1;}}
    release(p);return rc<0?-1:0;
}
static int resolve_view(PjObject *parent,const char *label,PjObject *out,ViewWalk *previous)
{
    int mode;long long writer;if(viewinfo(parent,&mode,&writer))return -1;
    if(!mode)return native_resolve(parent,label,out);
    for(ViewWalk *p=previous;p;p=p->previous)if(sameid(&p->id,&parent->id)){errno=ELOOP;return -1;}
    ViewWalk walk={parent->id,previous};unsigned char *p;size_t n,raw,meta;ProjectRecord rec;ProjectEntry e;
    if(pj_blob(parent,&p,&n,&raw,&meta))return -1;
    if(project_record_open(&rec,p,raw)){release(p);errno=EIO;return -1;}
    PjObject chosen={0},writable={0};unsigned rights=~0u;size_t found=0;int rc=-1;
    for(size_t i=0;i<rec.count;i++){
        if(project_record_entry(&rec,i,&e))goto finish;
        char source[256];memcpy(source,e.name,e.name_length);source[e.name_length]=0;
        PjObject root={0},item={0};if(native_resolve(parent,source,&root))goto finish;
        int got=resolve_view(&root,label,&item,&walk);pj_close(&root);
        if(got){if(errno!=ENOENT || mode==2)goto finish;continue;}
        if(found && !sameid(&chosen.id,&item.id)){pj_close(&item);errno=ENOTUNIQ;goto finish;}
        rights&=item.rights;found++;
        if((long long)i==writer)writable=item;
        else if(!chosen.handle)chosen=item;
        else pj_close(&item);
        if(!chosen.handle && writable.handle){ /* keep the selected grant separately */
            ObjProjectReq copy={0};copy.operation=ProjectAttenuate;copy.rights=writable.rights;
            int h=pj_call(writable.handle,&copy,NULL,0);if(h<0)goto finish;
            if(pj_from_handle(&chosen,h)){objclose(h);goto finish;}
        }
    }
    if(!found){errno=ENOENT;goto finish;}
    if(writable.handle){if(!sameid(&chosen.id,&writable.id)){errno=ENOTUNIQ;goto finish;}rights=writable.rights;}
    else rights&=Vread|Vmeta|Vdelegate|Vexecute;
    ObjProjectReq attenuate={0};attenuate.operation=ProjectAttenuate;attenuate.rights=rights;
    int h=pj_call(writable.handle?writable.handle:chosen.handle,&attenuate,NULL,0);if(h<0)goto finish;
    rc=pj_from_handle(out,h);if(rc)objclose(h);
finish:
    pj_close(&chosen);pj_close(&writable);release(p);return rc;
}
int pj_resolve(PjObject *parent,const char *label,PjObject *out){return resolve_view(parent,label,out,NULL);}
static int write_parent(PjObject *parent,PjObject *out)
{
    int mode;long long writer;if(viewinfo(parent,&mode,&writer))return -1;
    if(!mode){ObjProjectReq r={0};r.operation=ProjectAttenuate;r.rights=parent->rights;int h=pj_call(parent->handle,&r,NULL,0);return h<0?-1:pj_from_handle(out,h);}
    if(writer<0){errno=EROFS;return -1;}
    unsigned char *p;size_t n,raw,meta;ProjectRecord rec;ProjectEntry e;
    if(pj_blob(parent,&p,&n,&raw,&meta))return -1;
    if(project_record_open(&rec,p,raw) || (uint64_t)writer>=rec.count || project_record_entry(&rec,(size_t)writer,&e)){release(p);errno=EIO;return -1;}
    char label[256];memcpy(label,e.name,e.name_length);label[e.name_length]=0;release(p);
    return native_resolve(parent,label,out);
}
static int directory_metadata(PjObject *o,unsigned char **out,size_t *length,uint64_t *epoch)
{
    unsigned char *p;size_t n,raw,meta;
    if(pj_blob(o,&p,&n,&raw,&meta))return -1;
    ProjectRecord record;if(project_record_open(&record,p,raw)){release(p);errno=EIO;return -1;}
    UxMeta m={0};if(uxobject_decode(p+raw,(unsigned)meta,&m)){release(p);errno=EIO;return -1;}
    m.mtime_ns=m.ctime_ns=(long long)_nsec();m.known|=UXMETA_MTIME|UXMETA_CTIME;
    unsigned char *data=malloc(UXMETA_ARENA);if(!data){release(p);errno=ENOMEM;return -1;}
    unsigned size;int rc=uxobject_metadata(&m,p+raw,(unsigned)meta,data,UXMETA_ARENA,&size);release(p);
    if(rc){release(data);errno=EIO;return -1;}
    *epoch=record.epoch;*out=data;*length=size;return 0;
}
static int mutate(PjObject *parent,ObjProjectReq *r)
{
    for(;;){
        unsigned char *meta;size_t n;uint64_t epoch;
        if(directory_metadata(parent,&meta,&n,&epoch))return -1;
        if(r->epoch && r->epoch!=epoch){release(meta);errno=EAGAIN;return -1;}
        r->metadata=meta;r->metadata_length=n;r->epoch=epoch;
        int rc=pj_call(parent->handle,r,NULL,0);release(meta);r->metadata=NULL;r->metadata_length=0;
        if(rc<0 && errno==EAGAIN && (r->operation==ProjectCreate || r->operation==ProjectLink)){r->epoch=0;continue;}
        return rc;
    }
}
static int native_create(PjObject *parent,const char *label,unsigned kind,const void *data,size_t n,PjObject *out)
{
    UxMeta m;uxmeta_init(&m,label,kind==VkindObjectSpace?1:2,_nsec());
    unsigned char *p;size_t len;
    if(kind==VkindObjectSpace){if(pj_empty(&m,&p,&len))return -1;}
    else if(framed(data,n,&m,NULL,0,&p,&len))return -1;
    ObjProjectReq r={0};r.operation=ProjectCreate;r.kind=kind;r.rights=parent->rights;
    if(name(&r,label)){release(p);return -1;}
    r.data=p;r.length=len;int rc=mutate(parent,&r);release(p);
    if(rc<0)return -1;return out?pj_resolve(parent,label,out):0;
}
static int native_link(PjObject *parent,const char *label,PjObject *target)
{
    ObjProjectReq r={0};r.operation=ProjectLink;r.flags=1;r.other=target->handle;r.rights=target->rights&parent->rights;
    if(name(&r,label))return -1;return mutate(parent,&r)<0?-1:0;
}
static int binding(PjObject *parent,const char *label,ObjProjectReq *r)
{
    unsigned char *p;size_t n,raw,meta;ProjectRecord rec;ProjectEntry e;
    if(pj_blob(parent,&p,&n,&raw,&meta))return -1;
    if(project_record_open(&rec,p,raw)){release(p);errno=EIO;return -1;}
    int found=0;size_t len=strlen(label);
    for(size_t i=0;i<rec.count;i++){
        if(project_record_entry(&rec,i,&e)){release(p);errno=EIO;return -1;}
        if(e.name_length==len && !memcmp(e.name,label,len)){found++;r->binding=e.binding;}
    }
    r->epoch=rec.epoch;release(p);
    if(found!=1){errno=found?ENOTUNIQ:ENOENT;return -1;}return 0;
}
static int native_remove(PjObject *parent,const char *label,int directory)
{
    PjObject target={0};if(pj_resolve(parent,label,&target))return -1;
    if((target.kind==VkindObjectSpace)!=directory){pj_close(&target);errno=directory?ENOTDIR:EISDIR;return -1;}
    if(directory){unsigned char *p;size_t n,raw,meta;ProjectRecord rec;
        if(pj_blob(&target,&p,&n,&raw,&meta)){pj_close(&target);return -1;}
        int rc=project_record_open(&rec,p,raw);release(p);if(rc || rec.count){pj_close(&target);errno=rc?EIO:ENOTEMPTY;return -1;}}
    pj_close(&target);ObjProjectReq r={0};r.operation=ProjectUnlink;r.kind=directory?VkindObjectSpace:VkindGe;
    if(binding(parent,label,&r))return -1;return mutate(parent,&r);
}
static int native_rename(PjObject *src,const char *old,PjObject *dst,const char *label,int replace)
{
    PjObject from={0},to={0};
    if(native_resolve(src,old,&from))return -1;
    int present=native_resolve(dst,label,&to)==0;
    if(!present && errno!=ENOENT){pj_close(&from);return -1;}
    if(present){
        if((from.kind==VkindObjectSpace)!=(to.kind==VkindObjectSpace)){pj_close(&from);pj_close(&to);errno=from.kind==VkindObjectSpace?ENOTDIR:EISDIR;return -1;}
        if(to.kind==VkindObjectSpace && !sameid(&from.id,&to.id)){
            unsigned char *data;size_t n,raw,meta;ProjectRecord rec;
            if(pj_blob(&to,&data,&n,&raw,&meta)){pj_close(&from);pj_close(&to);return -1;}
            int rc=project_record_open(&rec,data,raw);release(data);
            if(rc || rec.count){pj_close(&from);pj_close(&to);errno=rc?EIO:ENOTEMPTY;return -1;}
        }
    }
    pj_close(&from);pj_close(&to);
    ObjProjectReq r={0};r.operation=ProjectRename;r.other=dst->handle;r.replace=replace;r.kind=from.kind;
    if(name(&r,label)||binding(src,old,&r))return -1;
    unsigned char *meta;size_t n;uint64_t epoch;
    if(directory_metadata(dst,&meta,&n,&epoch))return -1;
    r.other_metadata=meta;r.other_metadata_length=n;r.other_epoch=epoch;
    int rc=mutate(src,&r);release(meta);return rc;
}
int pj_metadata_size(PjObject *o,UxMeta *m,size_t *raw)
{
    ObjProjectReq r={0};r.operation=ProjectMetadata;
    int n=pj_call(o->handle,&r,NULL,0);if(n<0)return -1;
    unsigned char *p=malloc(n?n:1);if(!p){errno=ENOMEM;return -1;}
        ObjCall c={0};c.version=OBJ_ABI_VERSION;c.op=ObjOpProject;c.in=&r;c.inlen=sizeof r;c.out=p;c.outlen=(size_t)n;
    if(objinvoke(o->handle,&c,sizeof c)<0){release(p);return failure();}
    if(c.result!=n){release(p);errno=EAGAIN;return -1;}
    if(raw)*raw=(size_t)c.aux;
    memset(m,0,sizeof *m);int rc=uxobject_decode(p,(unsigned)n,m);release(p);
    if(rc){errno=EIO;return -1;}return 0;
}
int pj_metadata(PjObject *o,UxMeta *m){return pj_metadata_size(o,m,NULL);}
long pj_read(PjObject *o,void *dst,size_t size,uint64_t off)
{
    unsigned char *p;size_t n,raw,meta;if(pj_blob(o,&p,&n,&raw,&meta))return -1;
    if(o->kind!=VkindGe){release(p);errno=EISDIR;return -1;}
    if(off>=raw)size=0;else if(size>raw-off)size=raw-off;
    if(size)memcpy(dst,p+off,size);release(p);return (long)size;
}
long pj_write(PjObject *o,const void *src,size_t size,uint64_t off,int truncate)
{
    unsigned char *p,*q,*data;size_t n,raw,meta,len,total;
retry:
    if(pj_blob(o,&p,&n,&raw,&meta))return -1;
    if(truncate==2)off=raw;
    if(o->kind!=VkindGe || off>64u*1024u*1024u || size>64u*1024u*1024u-off){release(p);errno=EFBIG;return -1;}
    len=(size_t)off+size;if(truncate!=1 && raw>len)len=raw;
    data=calloc(len?len:1,1);if(!data){release(p);errno=ENOMEM;return -1;}
    if(truncate!=1 && raw)memcpy(data,p,raw);if(size)memcpy(data+off,src,size);
    UxMeta m;memset(&m,0,sizeof m);if(uxobject_decode(p+raw,(unsigned)meta,&m)){release(p);release(data);errno=EIO;return -1;}
    m.mtime_ns=m.ctime_ns=(long long)_nsec();m.known|=UXMETA_MTIME|UXMETA_CTIME;
    int rc=framed(data,len,&m,p+raw,(unsigned)meta,&q,&total);release(data);release(p);if(rc)return -1;
    ObjProjectReq r={0};r.operation=ProjectPut;r.data=q;r.length=total;r.epoch=o->version;rc=pj_call(o->handle,&r,NULL,0);release(q);if(rc<0 && errno==EAGAIN)goto retry;if(rc>=0)o->position_after=off+size;return rc<0?-1:(long)size;
}

int pj_create(PjObject *parent,const char *label,unsigned kind,const void *data,size_t n,PjObject *out)
{PjObject w={0};if(write_parent(parent,&w))return -1;int rc=native_create(&w,label,kind,data,n,out);pj_close(&w);return rc;}
int pj_link(PjObject *parent,const char *label,PjObject *target)
{PjObject w={0};if(write_parent(parent,&w))return -1;int rc=native_link(&w,label,target);pj_close(&w);return rc;}
int pj_remove(PjObject *parent,const char *label,int directory)
{PjObject w={0};if(write_parent(parent,&w))return -1;int rc=native_remove(&w,label,directory);pj_close(&w);return rc;}
int pj_rename(PjObject *src,const char *old,PjObject *dst,const char *label,int replace)
{PjObject a={0},b={0};if(write_parent(src,&a))return -1;if(write_parent(dst,&b)){pj_close(&a);return -1;}int rc=native_rename(&a,old,&b,label,replace);pj_close(&a);pj_close(&b);return rc;}
typedef struct {char *label;ProjectEntry entry;ProjectId id;} ListItem;
typedef struct {ListItem *items;size_t count;} Listing;
static int collect(void *context,const ProjectEntry *entry)
{
    Listing *list=context;
    for(size_t i=0;i<list->count;i++)if(list->items[i].entry.oid==entry->oid && list->items[i].entry.name_length==entry->name_length && !memcmp(list->items[i].label,entry->name,entry->name_length))return 0;
    ListItem *items=realloc(list->items,(list->count+1)*sizeof *items);if(!items)return -1;list->items=items;
    ListItem *item=&items[list->count];memset(item,0,sizeof *item);item->label=malloc(entry->name_length+1);if(!item->label)return -1;
    memcpy(item->label,entry->name,entry->name_length);item->label[entry->name_length]=0;item->entry=*entry;item->entry.name=(const unsigned char*)item->label;list->count++;return 0;
}
typedef struct {const ProjectEntry *entry;int found;} Membership;
static int member(void *context,const ProjectEntry *e)
{
    Membership *m=context;
    if(e->oid==m->entry->oid && e->name_length==m->entry->name_length && !memcmp(e->name,m->entry->name,e->name_length))m->found=1;
    return 0;
}
static int list_view(PjObject *parent,int (*emit)(void*,const ProjectEntry*),void *context,ViewWalk *previous)
{
    int mode;long long writer;if(viewinfo(parent,&mode,&writer))return -1;
    for(ViewWalk *w=previous;w;w=w->previous)if(sameid(&w->id,&parent->id)){errno=ELOOP;return -1;}
    ViewWalk walk={parent->id,previous};unsigned char *p;size_t n,raw,meta;ProjectRecord rec;ProjectEntry e;
    if(pj_blob(parent,&p,&n,&raw,&meta))return -1;
    if(project_record_open(&rec,p,raw)){release(p);errno=EIO;return -1;}
    Listing list={0};int rc=0;
    for(size_t i=0;i<rec.count;i++){
        if(project_record_entry(&rec,i,&e)){rc=-1;break;}
        if(!mode){if(emit(context,&e)){rc=-1;break;}continue;}
        char name[256];memcpy(name,e.name,e.name_length);name[e.name_length]=0;
        PjObject source={0};if(native_resolve(parent,name,&source)){rc=-1;break;}
        rc=list_view(&source,collect,&list,&walk);pj_close(&source);if(rc)break;
    }
    if(mode && !rc)for(size_t i=0;i<list.count;i++){
        if(mode==2){
            int common=1;
            for(size_t j=0;j<rec.count;j++){
                ProjectEntry source_entry;if(project_record_entry(&rec,j,&source_entry)){rc=-1;break;}
                char source_name[256];memcpy(source_name,source_entry.name,source_entry.name_length);source_name[source_entry.name_length]=0;
                PjObject source={0};if(native_resolve(parent,source_name,&source)){rc=-1;break;}
                Membership membership={&list.items[i].entry,0};
                rc=list_view(&source,member,&membership,&walk);pj_close(&source);
                if(rc)break;
                if(!membership.found){common=0;break;}
            }
            if(rc)break;
            if(!common)continue;
        }
        if(emit(context,&list.items[i].entry)){rc=-1;break;}
    }
    for(size_t i=0;i<list.count;i++)release(list.items[i].label);release(list.items);release(p);return rc;
}
int pj_list(PjObject *parent,int (*emit)(void*,const ProjectEntry*),void *context){return list_view(parent,emit,context,NULL);}
