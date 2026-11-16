#include "uxobject.h"
#define OBJTAIL_NATIVE_TYPES
#include "objtail.h"

static unsigned length(const char *p){unsigned n=0;while(p[n])n++;return n;}
static void copy(void*d,const void*s,unsigned n){unsigned char*a=d;const unsigned char*b=s;while(n--)*a++=*b++;}
static void clear(void*d,unsigned n){unsigned char*p=d;while(n--)*p++=0;}
void uxmeta_init(UxMeta*m,const char*path,unsigned kind,unsigned long long now)
{
    clear(m,sizeof(*m));const char *name=path;
    for(const char*p=path;*p;p++)if(*p=='/' && p[1])name=p+1;
    unsigned n=length(name);if(n>255)n=255;copy(m->name,name,n);
    const char *type=kind==1?"inode/directory":"application/octet-stream";
    copy(m->media_type,type,length(type)+1);m->mode=kind==1?0040700:0100600;
    if(now){m->atime_ns=m->mtime_ns=m->ctime_ns=m->birth_ns=(long long)now;m->known=15;}
}
/* Definite-length CBOR walker. Bounds and recursion are checked before use. */
typedef struct {const unsigned char *p,*end;} Reader;
static int header(Reader*r,unsigned*k,unsigned long long*n)
{
    if(r->p==r->end)return -1;
    unsigned b=*r->p++,a=b&31;*k=b>>5;
    if(a<24){*n=a;return 0;}
    unsigned width=a==24?1:a==25?2:a==26?4:a==27?8:0;
    if(!width || (unsigned long long)(r->end-r->p)<width)return -1;
    *n=0;while(width--)*n=(*n<<8)|*r->p++;return 0;
}
static int skip(Reader*r,unsigned depth)
{
    unsigned k;unsigned long long n;
    if(depth>64 || header(r,&k,&n))return -1;
    if(k==2 || k==3){if(n>(unsigned long long)(r->end-r->p))return -1;r->p+=n;return 0;}
    if(k==4 || k==5){if(n>(unsigned long long)(r->end-r->p))return -1;if(k==5)n*=2;while(n--)if(skip(r,depth+1))return -1;}
    else if(k>5 && k!=7)return -1;
    return 0;
}
static int field(const unsigned char*data,unsigned size,const char*key,Reader*out)
{
    Reader r={data,data+size};unsigned k;unsigned long long count,n;
    if(header(&r,&k,&count)||k!=5||count>size)return -1;
    int found=0;
    while(count--){
        Reader keyr=r;if(skip(&r,0))return -1;
        const unsigned char *val=r.p;if(skip(&r,0))return -1;
        if(header(&keyr,&k,&n)||k!=3 || n>(unsigned long long)(keyr.end-keyr.p))continue;
        if(n==length(key)){
            unsigned i;for(i=0;i<n && keyr.p[i]==(unsigned char)key[i];i++){}
            if(i==n){if(found)return -1;out->p=val;out->end=r.p;found=1;}
        }
    }
    return r.p==r.end?found:-1;
}
typedef struct {unsigned char *p;unsigned cap,used;int bad;} Writer;
static void put(Writer*w,const void*p,unsigned n){if(n>w->cap-w->used){w->bad=1;return;}copy(w->p+w->used,p,n);w->used+=n;}
static void emithead(Writer*w,unsigned k,unsigned long long n)
{
    unsigned char b[9];unsigned width=n<24?0:n<256?1:n<65536?2:n<0x100000000ULL?4:8;
    b[0]=(unsigned char)(k*32+(width==0?n:width==1?24:width==2?25:width==4?26:27));
    for(unsigned i=0;i<width;i++)b[1+i]=(unsigned char)(n>>(8*(width-1-i)));
    put(w,b,width+1);
}
static void text(Writer*w,const char*s){unsigned n=length(s);emithead(w,3,n);put(w,s,n);}
static void num(Writer*w,const char*key,unsigned long long n){text(w,key);emithead(w,0,n);}
static void timefield(Writer*w,const char*key,long long n,int known){text(w,key);if(!known){unsigned char nil=0xf6;put(w,&nil,1);}else if(n<0)emithead(w,1,(unsigned long long)(-(n+1)));else emithead(w,0,(unsigned long long)n);}
/* Merge raw key/value slices for unknown keys. Definite maps only; other
 * metadata remains entirely opaque and is emitted unchanged. */
static int mapmerge(const unsigned char*known,unsigned kn,const unsigned char*old,unsigned on,Writer*out)
{
    Reader r={old,old+on},start;unsigned k;unsigned long long count,total=0;
    if(header(&r,&k,&count)||k!=5||count>on)return -1;
    start=r;
    for(unsigned pass=0;pass<2;pass++){
        r=start;unsigned long long left=count;
        while(left--){const unsigned char *begin=r.p;Reader kr=r;if(skip(&r,0))return -1;Reader value=r;if(skip(&r,0))return -1;
            unsigned kk;unsigned long long len;int exists=0;
            if(!header(&kr,&kk,&len) && kk==3 && len<64 && len<=(unsigned long long)(kr.end-kr.p)){
                char key[64];copy(key,kr.p,(unsigned)len);key[len]=0;Reader unused;exists=field(known,kn,key,&unused)>0;
            }
            (void)value;
            if(!exists){if(pass)put(out,begin,(unsigned)(r.p-begin));else total++;}
        }
        if(r.p!=r.end)return -1;
        if(!pass){Reader base={known,known+kn};unsigned long long cnt;if(header(&base,&k,&cnt)||k!=5)return -1;emithead(out,5,cnt+total);put(out,base.p,(unsigned)(base.end-base.p));}
    }
    return out->bad?-1:0;
}
int uxobject_metadata(const UxMeta*m,const unsigned char*old,unsigned on,unsigned char*out,unsigned cap,unsigned*size)
{
    static unsigned char base[UXMETA_ARENA],unixmerged[UXMETA_ARENA];
    unsigned char unixbase[256];
    Writer u={unixbase,sizeof(unixbase),0,0};emithead(&u,5,8);
    num(&u,"mode",m->mode);num(&u,"uid",m->uid);num(&u,"gid",m->gid);num(&u,"rdev",m->rdev);
    timefield(&u,"atime_ns",m->atime_ns,m->known&UXMETA_ATIME);timefield(&u,"mtime_ns",m->mtime_ns,m->known&UXMETA_MTIME);timefield(&u,"ctime_ns",m->ctime_ns,m->known&UXMETA_CTIME);timefield(&u,"birthtime_ns",m->birth_ns,m->known&UXMETA_BIRTH);
    Writer um={unixmerged,sizeof unixmerged,0,0};Reader previous;
    if(on && field(old,on,"unix",&previous)>0){if(mapmerge(unixbase,u.used,previous.p,(unsigned)(previous.end-previous.p),&um))return -1;}
    else put(&um,unixbase,u.used);
    if(u.bad||um.bad)return -1;
    Reader oldcreated;
    int keepcreated=on && !(m->known&UXMETA_BIRTH) && field(old,on,"created",&oldcreated)>0;
    Writer w={base,sizeof base,0,0};emithead(&w,5,keepcreated?6:7);text(&w,"profile");text(&w,"plan2001.unix.v1");
    text(&w,"name_bytes");unsigned n=length(m->name);emithead(&w,2,n);put(&w,m->name,n);
    text(&w,"name");int ascii=1;for(unsigned i=0;i<n;i++)if((unsigned char)m->name[i]>=128)ascii=0;
    if(ascii)text(&w,m->name);else {unsigned char nil=0xf6;put(&w,&nil,1);}
    if(!keepcreated)timefield(&w,"created",m->birth_ns,m->known&UXMETA_BIRTH);
    /* A source digest describes old raw bytes. Do not retain it as a current
     * content digest after edits; this producer leaves it explicitly unknown. */
    {unsigned char nil=0xf6;text(&w,"raw_sha256");put(&w,&nil,1);}
    text(&w,"media_type");text(&w,m->media_type);text(&w,"unix");put(&w,unixmerged,um.used);
    if(w.bad)return -1;
    Writer dst={out,cap,0,0};
    if(on){
        Reader map={old,old+on};unsigned k;unsigned long long count;
        Reader valid=map;
        if(skip(&valid,0)||valid.p!=valid.end||header(&map,&k,&count)||k!=5){put(&dst,old,on);}
        else if(mapmerge(base,w.used,old,on,&dst))return -1;
    }else put(&dst,base,w.used);
    if(dst.bad)return -1;
    *size=dst.used;return 0;
}
static int getnum(Reader r,long long *v)
{
    unsigned k;unsigned long long n;if(header(&r,&k,&n)||r.p!=r.end||k>1||n>0x7fffffffffffffffULL)return -1;
    *v=k==0?(long long)n:-1-(long long)n;return 0;
}
static int getstr(Reader r,char*out,unsigned cap)
{
    unsigned k;unsigned long long n;if(header(&r,&k,&n)||(k!=2&&k!=3)||n>=cap||n!=(unsigned long long)(r.end-r.p))return -1;
    for(unsigned i=0;i<n;i++)if(!r.p[i])return -1;
    copy(out,r.p,(unsigned)n);out[n]=0;return 0;
}
int uxobject_decode(const unsigned char*data,unsigned size,UxMeta*m)
{
    Reader v,u;long long n;int rc;
    Reader all={data,data+size};if(skip(&all,0)||all.p!=all.end)return 0; /* opaque metadata */
    rc=field(data,size,"name_bytes",&v);if(rc<0)return -1;
    if(rc){if(getstr(v,m->name,sizeof m->name))return -1;}
    else {rc=field(data,size,"name",&v);if(rc<0)return -1;
        if(rc && !(v.end-v.p==1 && *v.p==0xf6) && getstr(v,m->name,sizeof m->name))return -1;}
    rc=field(data,size,"media_type",&v);if(rc<0)return -1;
    if(rc && getstr(v,m->media_type,sizeof m->media_type))return -1;
    if(field(data,size,"created",&v)>0 && getnum(v,&n)==0){m->birth_ns=n;m->known|=UXMETA_BIRTH;}
    if(field(data,size,"unix",&u)<=0)return 0;
    const char *keys[]={"mode","uid","gid","rdev","atime_ns","mtime_ns","ctime_ns","birthtime_ns"};
    for(unsigned i=0;i<8;i++){
        rc=field(u.p,(unsigned)(u.end-u.p),keys[i],&v);if(rc<0)return -1;if(!rc)continue;
        if(getnum(v,&n)){if(v.end-v.p==1 && *v.p==0xf6)continue;return -1;}
        if(i<3 && (n<0 || (unsigned long long)n>0xffffffffULL))return -1;
        if(i==0)m->mode=(unsigned)n;else if(i==1)m->uid=(unsigned)n;else if(i==2)m->gid=(unsigned)n;
        else if(i==3){if(n<0)return -1;m->rdev=(unsigned long long)n;}
        else {unsigned bit=1u<<(i-4);m->known|=bit;if(i==4)m->atime_ns=n;else if(i==5)m->mtime_ns=n;else if(i==6)m->ctime_ns=n;else m->birth_ns=n;}
    }
    return 0;
}
int uxobject_split(const void*buf,unsigned size,unsigned*raw,unsigned*meta)
{
    uint64_t r,m;if(size<ObjTailSize||objtail_parse((const unsigned char*)buf+size-ObjTailSize,size,&r,&m))return -1;
    *raw=(unsigned)r;*meta=(unsigned)m;return 0;
}
int uxobject_footer(unsigned char*out,unsigned raw,unsigned meta){return objtail_pack(out,raw,meta);}
int uxobject_text_field(const unsigned char *data,unsigned n,const char *key,char *out,unsigned cap)
{
    Reader r;int rc=field(data,n,key,&r);if(rc<=0)return rc;
    return getstr(r,out,cap)?-1:1;
}
int uxobject_int_field(const unsigned char *data,unsigned n,const char *key,long long *out)
{
    Reader r;int rc=field(data,n,key,&r);if(rc<=0)return rc;
    if(r.end-r.p==1 && *r.p==0xf6)return 0;
    return getnum(r,out)?-1:1;
}
