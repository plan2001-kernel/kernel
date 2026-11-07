/* GE relations R1: ordinary native OIDs + one versioned metadata object.
 * No filesystem, Python, kernel types, allocator or second storage engine.
 */
#include "relations.h"
#ifndef GR_KERNEL
#include <string.h>
#endif

static const unsigned char magic[8]={'G','E','R','E','L','R','1',0};
static void p32(unsigned char *p,uint32_t v) {for(int i=3;i>=0;i--){p[i]=(unsigned char)v;v>>=8;}}
static void p64(unsigned char *p,uint64_t v) {for(int i=7;i>=0;i--){p[i]=(unsigned char)v;v>>=8;}}
static uint32_t g32(const unsigned char *p) {uint32_t v=0;for(int i=0;i<4;i++)v=(v<<8)|p[i];return v;}
static uint64_t g64(const unsigned char *p) {uint64_t v=0;for(int i=0;i<8;i++)v=(v<<8)|p[i];return v;}
static int white(unsigned char c) {return c==' '||c=='\t'||c=='\n'||c=='\r'||c=='\f'||c=='\v';}
static int tagvalid(const char *s,size_t n) {
    if(!s||n<2||n>=GR_MAX_NAME||s[0]!='#')return 0;
    for(size_t i=0;i<n;){
        unsigned char c=(unsigned char)s[i++];
        if(!c||white(c))return 0;
        if(c<128)continue;
        unsigned count;uint32_t v,min;
        if(c>=0xc2&&c<=0xdf){count=1;v=c&31;min=0x80;}
        else if(c>=0xe0&&c<=0xef){count=2;v=c&15;min=0x800;}
        else if(c>=0xf0&&c<=0xf4){count=3;v=c&7;min=0x10000;}
        else return 0;
        if(count>n-i)return 0;
        while(count--){c=(unsigned char)s[i++];if((c&0xc0)!=0x80)return 0;v=(v<<6)|(c&63);}
        if(v<min||v>0x10ffff||(v>=0xd800&&v<=0xdfff))return 0;
    }
    return 1;
}
static int oidvalid(uint64_t oid) {
    return oid>=64&&oid<UINT64_C(0xffffffff00000000)&&
        !(oid>=UINT64_C(0x4f45584543000001)&&oid<=UINT64_C(0x4f45584543000003));
}
static int object(const GrState *s,uint64_t oid) {
    for(unsigned i=0;i<s->nobjects;i++)if(s->objects[i]==oid)return (int)i;
    return -1;
}
static int target(const GrState *s,uint64_t oid) {
    for(unsigned i=0;i<s->ntags;i++)if(s->tags[i].oid==oid)return (int)i;
    return -1;
}
static int statevalid(const GrState *s) {
    if(s->nobjects>GR_MAX_OBJECTS||s->ntags>GR_MAX_TAGS||s->nedges>GR_MAX_EDGES)return 0;
    for(unsigned i=0;i<s->nobjects;i++){
        if(!oidvalid(s->objects[i]))return 0;
        for(unsigned j=0;j<i;j++)if(s->objects[j]==s->objects[i])return 0;
    }
    for(unsigned i=0;i<s->ntags;i++){
        size_t n=0;while(n<GR_MAX_NAME&&s->tags[i].name[n])n++;
        if(!tagvalid(s->tags[i].name,n)||object(s,s->tags[i].oid)<0)return 0;
        for(unsigned j=0;j<i;j++)if(s->tags[j].oid==s->tags[i].oid||
            !strcmp(s->tags[j].name,s->tags[i].name))return 0;
    }
    for(unsigned i=0;i<s->nedges;i++){
        GrEdge e=s->edges[i];
        if(object(s,e.source)<0||target(s,e.target)<0)return 0;
        for(unsigned j=0;j<i;j++)if(s->edges[j].source==e.source&&s->edges[j].target==e.target)return 0;
    }
    return 1;
}
int gr_encode(const GrState *s,void *buffer,size_t cap,size_t *written) {
    if(!s||!buffer||!written||!statevalid(s))return GR_INVALID;
    size_t n=32+8*s->nobjects+16*s->nedges;
    for(unsigned i=0;i<s->ntags;i++)n+=12+strlen(s->tags[i].name);
    if(n>cap||n>GR_MAX_BLOB)return GR_LIMIT;
    unsigned char *p=buffer;memset(p,0,32);memcpy(p,magic,8);
    p32(p+8,1);p32(p+16,s->nobjects);p32(p+20,s->ntags);p32(p+24,s->nedges);p+=32;
    for(unsigned i=0;i<s->nobjects;i++){p64(p,s->objects[i]);p+=8;}
    for(unsigned i=0;i<s->ntags;i++){
        size_t len=strlen(s->tags[i].name);
        p64(p,s->tags[i].oid);p32(p+8,(uint32_t)len);memcpy(p+12,s->tags[i].name,len);p+=12+len;
    }
    for(unsigned i=0;i<s->nedges;i++){p64(p,s->edges[i].source);p64(p+8,s->edges[i].target);p+=16;}
    *written=n;return GR_OK;
}
int gr_decode(GrState *s,const void *buffer,size_t n) {
    if(!s||!buffer||n<32||n>GR_MAX_BLOB)return GR_CORRUPT;
    const unsigned char *p=buffer;
    memset(s,0,sizeof *s);
    if(memcmp(p,magic,8)||g32(p+8)!=1||g32(p+12)||g32(p+28))return GR_CORRUPT;
    s->nobjects=g32(p+16);s->ntags=g32(p+20);s->nedges=g32(p+24);p+=32;n-=32;
    if(s->nobjects>GR_MAX_OBJECTS||s->ntags>GR_MAX_TAGS||s->nedges>GR_MAX_EDGES)return GR_CORRUPT;
    if(n<8*s->nobjects)return GR_CORRUPT;
    for(unsigned i=0;i<s->nobjects;i++){s->objects[i]=g64(p);p+=8;n-=8;}
    for(unsigned i=0;i<s->ntags;i++){
        if(n<12)return GR_CORRUPT;
        uint32_t len=g32(p+8);
        if(len>=GR_MAX_NAME||len>n-12||!tagvalid((const char*)p+12,len))return GR_CORRUPT;
        s->tags[i].oid=g64(p);memcpy(s->tags[i].name,p+12,len);p+=12+len;n-=12+len;
    }
    if(n!=16*s->nedges)return GR_CORRUPT;
    for(unsigned i=0;i<s->nedges;i++){s->edges[i].source=g64(p);s->edges[i].target=g64(p+8);p+=16;}
    return statevalid(s)?GR_OK:GR_CORRUPT;
}
static unsigned hash(const char *p,size_t n) {unsigned h=2166136261u;while(n--)h=(h^(unsigned char)*p++)*16777619u;return h;}
static int tagindex(const GrDB *d,const char *name,size_t n) {
    unsigned slot=hash(name,n)%(2*GR_MAX_TAGS);
    for(unsigned i=0;i<2*GR_MAX_TAGS;i++,slot=(slot+1)%(2*GR_MAX_TAGS)){
        int t=d->taghash[slot];if(t<0)return -1;
        if(strlen(d->state.tags[t].name)==n&&!memcmp(d->state.tags[t].name,name,n))return t;
    }
    return -1;
}
static void reindex(GrDB *d) {
    memset(d->postings,0,sizeof d->postings);
    for(unsigned i=0;i<2*GR_MAX_TAGS;i++)d->taghash[i]=-1;
    for(unsigned t=0;t<d->state.ntags;t++){
        unsigned slot=hash(d->state.tags[t].name,strlen(d->state.tags[t].name))%(2*GR_MAX_TAGS);
        while(d->taghash[slot]>=0)slot=(slot+1)%(2*GR_MAX_TAGS);
        d->taghash[slot]=(short)t;
    }
    for(unsigned i=0;i<d->state.nedges;i++){
        GrEdge e=d->state.edges[i];int o=object(&d->state,e.source),t=target(&d->state,e.target);
        d->postings[t][o/64]|=UINT64_C(1)<<(o%64);
    }
}
static int publish(GrDB *d) {
    unsigned char *blob=d->workspace;size_t n;
    int rc=gr_encode(&d->state,blob,GR_MAX_BLOB,&n);
    if(rc<0){d->valid=0;return rc;}
    if(d->backend.write(d->backend.arg,GR_META_OID,blob,n)||d->backend.commit(d->backend.arg)){
        d->valid=0;return GR_IO;
    }
    reindex(d);return GR_OK;
}
int gr_open(GrDB *d,const GrBackend *b) {
    if(!d||!b||!b->read||!b->create||!b->write||!b->commit)return GR_INVALID;
    /* b may point into d during a retry. */
    GrBackend saved=*b;memset(d,0,sizeof *d);d->backend=saved;
    unsigned char *blob=d->workspace;size_t n=0;
    int rc=b->read(b->arg,GR_META_OID,blob,GR_MAX_BLOB,&n);
    if(rc<0)return GR_IO;
    if(rc==0){
        if(gr_decode(&d->state,blob,n)<0)return GR_CORRUPT;
        for(unsigned i=0;i<d->state.ntags;i++){
            char name[GR_MAX_NAME];size_t len=0;
            if(b->read(b->arg,d->state.tags[i].oid,name,sizeof name,&len)||
               len!=strlen(d->state.tags[i].name)||memcmp(name,d->state.tags[i].name,len))return GR_CORRUPT;
        }
    }
    reindex(d);d->valid=1;return GR_OK;
}
int gr_create(GrDB *d,const void *data,size_t n,uint64_t *oid) {
    if(!d||!d->valid)return GR_IO;
    if(!oid||(!data&&n))return GR_INVALID;
    if(n>GR_MAX_DATA||d->state.nobjects==GR_MAX_OBJECTS)return GR_LIMIT;
    uint64_t id;
    if(d->backend.create(d->backend.arg,data,n,&id))return GR_IO;
    if(!oidvalid(id)||object(&d->state,id)>=0){d->valid=0;return GR_CORRUPT;}
    d->state.objects[d->state.nobjects++]=id;
    int rc=publish(d);if(!rc)*oid=id;return rc;
}
int gr_get(GrDB *d,uint64_t oid,void *buf,size_t cap,size_t *n) {
    if(!d||!d->valid)return GR_IO;
    if(!n)return GR_INVALID;
    if(object(&d->state,oid)<0)return GR_NOTFOUND;
    return d->backend.read(d->backend.arg,oid,buf,cap,n)==0?GR_OK:GR_IO;
}
int gr_add(GrDB *d,uint64_t oid,const char *name,size_t len) {
    if(!d||!d->valid)return GR_IO;
    if(!tagvalid(name,len))return GR_INVALID;
    if(object(&d->state,oid)<0)return GR_NOTFOUND;
    int t=tagindex(d,name,len);
    if(t>=0)for(unsigned i=0;i<d->state.nedges;i++)
        if(d->state.edges[i].source==oid&&d->state.edges[i].target==d->state.tags[t].oid)return GR_OK;
    if(d->state.nedges==GR_MAX_EDGES)return GR_LIMIT;
    if(t<0){
        if(d->state.ntags==GR_MAX_TAGS||d->state.nobjects==GR_MAX_OBJECTS)return GR_LIMIT;
        uint64_t id;
        if(d->backend.create(d->backend.arg,name,len,&id))return GR_IO;
        if(!oidvalid(id)||object(&d->state,id)>=0){d->valid=0;return GR_CORRUPT;}
        t=(int)d->state.ntags++;
        d->state.tags[t].oid=id;memcpy(d->state.tags[t].name,name,len);
        d->state.tags[t].name[len]=0;d->state.objects[d->state.nobjects++]=id;
    }
    GrEdge *e=&d->state.edges[d->state.nedges++];e->source=oid;e->target=d->state.tags[t].oid;
    return publish(d);
}
int gr_remove(GrDB *d,uint64_t oid,const char *name,size_t len) {
    if(!d||!d->valid)return GR_IO;
    if(!tagvalid(name,len))return GR_INVALID;
    if(object(&d->state,oid)<0)return GR_NOTFOUND;
    int t=tagindex(d,name,len);if(t<0)return GR_OK;
    for(unsigned i=0;i<d->state.nedges;i++)if(d->state.edges[i].source==oid&&
            d->state.edges[i].target==d->state.tags[t].oid){
        memmove(&d->state.edges[i],&d->state.edges[i+1],(d->state.nedges-i-1)*sizeof(GrEdge));
        d->state.nedges--;return publish(d);
    }
    return GR_OK;
}
const char *gr_tag_name(const GrDB *d,uint64_t oid) {
    if(!d||!d->valid)return NULL;
    int t=target(&d->state,oid);return t<0?NULL:d->state.tags[t].name;
}
int gr_tags(GrDB *d,uint64_t oid,uint64_t *out,size_t cap,size_t *n) {
    if(!d||!d->valid)return GR_IO;
    if(!out||!n)return GR_INVALID;
    if(object(&d->state,oid)<0)return GR_NOTFOUND;
    *n=0;
    for(unsigned i=0;i<d->state.nedges;i++)if(d->state.edges[i].source==oid){
        if(*n==cap)return GR_LIMIT;
        out[(*n)++]=d->state.edges[i].target;
    }
    return GR_OK;
}
int gr_find(GrDB *d,const char *query,size_t len,uint64_t *out,size_t cap,size_t *n) {
    if(!d||!d->valid)return GR_IO;
    if(!query||!out||!n||len>4096)return GR_INVALID;
    uint64_t bits[GR_WORDS]={0};int have=0,missing=0;
    for(size_t p=0;p<len;){
        while(p<len&&white((unsigned char)query[p]))p++;
        if(p==len)break;
        size_t start=p;while(p<len&&!white((unsigned char)query[p]))p++;
        if(!tagvalid(query+start,p-start))return GR_INVALID;
        int t=tagindex(d,query+start,p-start);
        if(t<0){missing=1;continue;}
        for(unsigned w=0;w<GR_WORDS;w++)bits[w]=have?(bits[w]&d->postings[t][w]):d->postings[t][w];
        have=1;
    }
    *n=0;if(!have||missing)return GR_OK;
    for(unsigned i=0;i<d->state.nobjects;i++)if(bits[i/64]&(UINT64_C(1)<<(i%64))){
        if(*n==cap)return GR_LIMIT;
        size_t p=(*n)++;uint64_t oid=d->state.objects[i];
        while(p&&out[p-1]>oid){out[p]=out[p-1];p--;}
        out[p]=oid;
    }
    return GR_OK;
}
