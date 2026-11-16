/* GE-backed ObjectSpace provider. The durable record is authoritative;
 * no per-process directory tree or file contents are mirrored. */
#define OBJTAIL_NATIVE_TYPES
#include "../../runtime/objtail.h"
static QLock projectgate;
static uvlong pjget(uchar *p,int n){uvlong v=0;while(n--)v=v<<8|*p++;return v;}
static void pjput(uchar *p,uvlong v,int n){while(n--){p[n]=v;v>>=8;}}
static uvlong pjhash(uchar *p,long n){uvlong h=0xcbf29ce484222325ULL;while(n--){h^=*p++;h*=1099511628211ULL;}return h;}
static long pjraw(uchar *p,long n)
{
    uint64_t raw,meta;
    if(n<32 || objtail_parse(p+n-32,n,&raw,&meta)<0)error("namespace: invalid object footer");
    return raw;
}
static long pjcheck(uchar *p,long n)
{
    long raw=pjraw(p,n),off=32,len;uvlong count,previous=0,id;
    if(raw<40 || pjget(p,8)!=PROJECT_MAGIC || pjget(p+8,8)==0 ||
       pjget(p+raw-8,8)!=pjhash(p,raw-8))error("namespace: corrupt record");
    count=pjget(p+24,8);
    if(count>(uvlong)(raw-40)/33)error("namespace: invalid count");
    while(count--){
        if(off>raw-40)error("namespace: short entry");
        id=pjget(p+off,8);len=pjget(p+off+28,4);
        if(id<=previous || id>pjget(p+16,8) || pjget(p+off+8,8)==0 ||
           pjget(p+off+16,4)==0 || (pjget(p+off+16,4)&~Vall) ||
           (pjget(p+off+24,4)!=VkindGe && pjget(p+off+24,4)!=VkindObjectSpace) ||
           len<1 || len>255 || len>raw-8-off-32)error("namespace: invalid entry");
        if(memchr(p+off+32,0,len)!=nil)error("namespace: NUL label");
        previous=id;off+=32+len;
    }
    if(off!=raw-8)error("namespace: trailing record bytes");
    return raw;
}
static uchar *pjload(Ohand *oh,uvlong oid,long *n,int directory)
{
    uchar *p=nil;
    if(waserror()){free(p);nexterror();}
    *n=osreadhandk(oh,oid,nil,0,nil);
    if(*n<32 || *n>64*1024*1024)error("namespace: invalid object length");
    p=mallocz(*n,1);if(p==nil)error(Enomem);
    if(osreadhandk(oh,oid,p,*n,nil)!=*n)error("namespace: changed object length");
    if(directory)pjcheck(p,*n);else pjraw(p,*n);
    poperror();return p;
}
static Ohand *pjbackend(Vobj *o,uvlong *oid)
{
    if(o->kind==VkindGe){*oid=o->oid;return o->priv;}
    if(o->kind!=VkindObjectSpace || o->priv==nil || ((Vspace*)o->priv)->project==nil)
        error("namespace: persistent object required");
    *oid=((Vspace*)o->priv)->projectoid;return ((Vspace*)o->priv)->project;
}
static int pjinstall(Ohand *backend,uvlong oid,int kind,int rights,Vcap *parent)
{
    Vobj *o=nil;Ohand *cap=nil;Vspace *s;int h;
    if(waserror()){oshandput(cap);vobjput(o);nexterror();}
    cap=kind==VkindObjectSpace?oscopycapdelegate(backend,oshandrights(backend)):oscopycap(backend,oid);
    o=vobjalloc(kind,0,0);if(o==nil)error(Enomem);
    if(kind==VkindObjectSpace){
        s=mallocz(sizeof *s,1);if(s==nil)error(Enomem);
        s->object=o;s->epoch=1;s->project=cap;cap=nil;s->projectoid=oid;o->priv=s;
        lock(&vspaceregistrylock);s->next=vspaceregistry;vspaceregistry=s;unlock(&vspaceregistrylock);
    }else{o->priv=cap;cap=nil;o->oid=oid;o->provider=VopenGe;}
    h=vobjinstallchild(o,rights,parent);poperror();vobjput(o);return h;
}
static void pjname(char *name){int n;for(n=0;n<256 && name[n];n++);if(n==0 || n==256)error(Ebadarg);}
/* Return an entry offset by BindingID, or exact unambiguous local name. */
static long pjfind(uchar *p,long raw,uvlong binding,char *name)
{
    long off=32,found=-1,len;int namelen=binding?0:strlen(name);
    while(off<raw-8){len=pjget(p+off+28,4);
        if((binding && pjget(p+off,8)==binding) ||
           (!binding && len==namelen && memcmp(name,p+off+32,len)==0)){
            if(found>=0)error("namespace: ambiguous name");found=off;
        }off+=32+len;
    }
    return found;
}
/* Copy on write the record. Preserve opaque object metadata verbatim. */
static uchar *pjedit(uchar *p,long n,long remove,uchar *add,long addlen,long *newn)
{
    long raw=pjcheck(p,n),oldlen=remove<0?0:32+pjget(p+remove+28,4);
    long newraw=raw-oldlen+addlen,meta=n-raw-32;uchar *q;
    if(newraw+meta+32>64*1024*1024)error("namespace: transaction size");
    q=mallocz(newraw+meta+32,1);if(q==nil)error(Enomem);
    memmove(q,p,32);
    if(remove<0)memmove(q+32,p+32,raw-40);
    else {memmove(q+32,p+32,remove-32);memmove(q+remove,p+remove+oldlen,raw-8-remove-oldlen);}
    if(addlen)memmove(q+newraw-8-addlen,add,addlen);
    pjput(q+8,pjget(p+8,8)+1,8);
    pjput(q+24,pjget(p+24,8)-(remove>=0)+(addlen>0),8);
    if(addlen)pjput(q+16,pjget(add,8),8);
    pjput(q+newraw-8,pjhash(q,newraw-8),8);
    memmove(q+newraw,p+raw,meta);objtail_pack(q+newraw+meta,newraw,meta);
    *newn=newraw+meta+32;return q;
}
/* Per-volume namespace reference index, published in the same GE transaction
 * as the changed bindings. An index OID grants no access to its targets. */
#define PJREFINDEX 0x4e53524546494e44ULL
static uchar *pjrefload(Ohand *oh,long *size)
{
    long n=osreadhandk(oh,PJREFINDEX,nil,0,nil);uchar *p;
    if(n<0){p=mallocz(56,1);if(p==nil)error(Enomem);memmove(p,"P2NREF01",8);pjput(p+16,pjhash(p,16),8);objtail_pack(p+24,24,0);*size=56;return p;}
    if(n<56 || n>64*1024*1024)error("namespace: invalid reference index size");
    p=mallocz(n,1);if(p==nil)error(Enomem);
    if(waserror()){free(p);nexterror();}
    if(osreadhandk(oh,PJREFINDEX,p,n,nil)!=n)error("namespace: short reference index");
    long raw=pjraw(p,n);uvlong count=pjget(p+8,8);
    if(raw<24 || memcmp(p,"P2NREF01",8) || count>(uvlong)(raw-24)/16 || raw!=24+16*count || pjget(p+raw-8,8)!=pjhash(p,raw-8))error("namespace: corrupt reference index");
    poperror();*size=n;return p;
}
static uvlong pjreferences(Ohand *oh,uvlong oid)
{
    long n;uchar *p=pjrefload(oh,&n);uvlong count=pjget(p+8,8),refs=0;
    for(uvlong i=0;i<count;i++)if(pjget(p+16+16*i,8)==oid){refs=pjget(p+24+16*i,8);break;}
    free(p);return refs;
}
static void pjrefchange(Ohand *tx,uvlong oid,int change)
{
    long n;uchar *old=pjrefload(tx,&n),*p=nil;uvlong count=pjget(old+8,8),i;
    if(waserror()){free(old);free(p);nexterror();}
    for(i=0;i<count;i++)if(pjget(old+16+16*i,8)==oid)break;
    long raw=24+16*(count+(i==count));
    p=mallocz(raw+32,1);if(p==nil)error(Enomem);memmove(p,old,16+16*count);
    uvlong refs=i==count?0:pjget(old+24+16*i,8);
    if((change<0 && refs==0) || (change>0 && refs==~(uvlong)0))error("namespace: invalid reference count");
    pjput(p+16+16*i,oid,8);pjput(p+24+16*i,refs+change,8);pjput(p+8,count+(i==count),8);
    pjput(p+raw-8,pjhash(p,raw-8),8);objtail_pack(p+raw,raw,0);
    oswritehandk(tx,PJREFINDEX,p,raw+32);poperror();free(old);free(p);
}
int vobjprojectopen(ObjProjectOpen *r,VobjId *id)
{
    Vhand *v=nil;Ohand *oh;uchar *p=nil;long n;int h,rights;uvlong oid;
    if(r->volume<=0 || (r->rights&~Vall))error(Ebadarg);
    v=vlookup(r->volume,Vread|Vdelegate|(r->oid?0:Vcreate|Vcommit));
    if(waserror()){free(p);vhandput(v);nexterror();}
    if(v->obj->kind!=VkindGeVolume)error(Ebadarg);oh=v->obj->priv;
    rights=r->rights?r->rights:Vread|Vmeta|Vdelegate;
    /* Directory control derives from volume write, not metadata or ownership. */
    if((rights&~(v->rights|((v->rights&Vwrite)?Vcontrol:0))))error(Eperm);
    oid=r->oid;
    if(!oid){
        if(r->length>64*1024*1024 || r->data==nil)error(Ebadarg);
        validaddr((uintptr)r->data,r->length,0);p=mallocz(r->length,1);if(p==nil)error(Enomem);
        memmove(p,r->data,r->length);pjcheck(p,r->length);oscreatehandowner(oh,p,r->length,&oid,up->owner?up->owner:ownerroot());
    }else p=pjload(oh,oid,&n,1);
    h=pjinstall(oh,oid,VkindObjectSpace,rights,v->cap);
    if(id!=nil)vobjidentity(h,id);
    poperror();free(p);vhandput(v);return h;
}
void vobjprojectcall(int handle,ObjCall *c)
{
    ObjProjectReq r;Vhand *h=nil,*other=nil;Ohand *oh,*tx=nil,*oh2;uvlong oid,oid2=0,newid,ver=0;
    uchar *p=nil,*p2=nil,*q=nil,*q2=nil,*payload=nil;long n=0,n2=0,raw,raw2=0,at=-1,at2=-1,qn=0,q2n=0;
    uchar add[32+255];long addlen=0;int rights,dir,need;
    if(c->in==nil || c->inlen!=sizeof r)error(Ebadarg);
    validaddr((uintptr)c->in,sizeof r,0);memmove(&r,c->in,sizeof r);
    if((r.flags && !(r.operation==ProjectLink && r.flags==1)) || r.reserved || r.length>64*1024*1024)error(Ebadarg);
    need=(r.operation==ProjectRead || r.operation==ProjectResolve || r.operation==ProjectLinks)?Vread:
         r.operation==ProjectAttenuate?Vdelegate:r.operation==ProjectPut?Vwrite:Vcontrol;
    h=vlookup(handle,need);
    if(waserror()){vhandput(h);nexterror();}
    oh=pjbackend(h->obj,&oid);oscheckhand(oh);dir=h->obj->kind==VkindObjectSpace;
    qlock(&projectgate);
    if(waserror()){
        free(p);free(p2);free(q);free(q2);free(payload);oshandput(tx);vhandput(other);
        qunlock(&projectgate);nexterror();
    }
    p=pjload(oh,oid,&n,dir);raw=pjraw(p,n);
    if(r.epoch && (!dir || r.epoch!=pjget(p+8,8)))error("namespace: epoch changed");
    c->aux=dir?pjget(p+8,8):0;c->result=0;
    if(r.operation==ProjectRead){
        c->result=n;
        if(c->outlen){if(c->out==nil || c->outlen<(uvlong)n)error("namespace: short buffer");validaddr((uintptr)c->out,n,1);memmove(c->out,p,n);}
        goto done;
    }
    if(r.operation==ProjectLinks){
        if(!dir)error(Ebadarg);
        uvlong target=oid;
        if(r.other){other=vlookup(r.other,Vmeta);oh2=pjbackend(other->obj,&target);uchar a[16],b[16];osvolumeid(oh,a);osvolumeid(oh2,b);if(memcmp(a,b,16))error("namespace: cross volume");}
        c->result=pjreferences(oh,target);goto done;
    }
    if(r.operation==ProjectAttenuate){
        rights=r.rights;if(!rights || (rights&~h->rights))error(Eperm);
        c->result=pjinstall(oh,oid,h->obj->kind,rights,h->cap);goto done;
    }
    if(r.operation==ProjectPut){
        if(dir)error("namespace: structural record is not a byte file");
        if(!r.length || r.data==nil)error(Ebadarg);validaddr((uintptr)r.data,r.length,0);
        payload=mallocz(r.length,1);if(payload==nil)error(Enomem);memmove(payload,r.data,r.length);pjraw(payload,r.length);
        tx=oscopycap(oh,oid);oswritehandk(tx,oid,payload,r.length);oscommithand(tx,&ver);c->aux=ver;goto done;
    }
    if(!dir)error(Ebadarg);
    if(r.operation==ProjectResolve){
        if(!r.binding)pjname(r.name);at=pjfind(p,raw,r.binding,r.name);
        if(at<0)error("namespace: not found");
        rights=pjget(p+at+16,4)&h->rights;
        if(r.rights){if(r.rights&~rights)error(Eperm);rights=r.rights;}
        if(!rights)error(Eperm);
        c->result=pjinstall(oh,pjget(p+at+8,8),pjget(p+at+24,4),rights,h->cap);goto done;
    }
    tx=oscopycapdelegate(oh,oshandrights(oh));
    if(r.operation==ProjectCreate || r.operation==ProjectDetached || r.operation==ProjectLink){
        if(r.operation!=ProjectDetached){pjname(r.name);if((r.operation==ProjectCreate || r.flags==1) && pjfind(p,raw,0,r.name)>=0)error("namespace: exists");}
        rights=r.rights?r.rights:h->rights;if(rights&~h->rights)error(Eperm);
        if(r.operation==ProjectCreate || r.operation==ProjectDetached){
            if(!(h->rights&Vcreate) || r.data==nil || !r.length ||
               (r.kind!=VkindGe && r.kind!=VkindObjectSpace))error(Eperm);
            validaddr((uintptr)r.data,r.length,0);payload=mallocz(r.length,1);if(payload==nil)error(Enomem);
            memmove(payload,r.data,r.length);if(r.kind==VkindObjectSpace){pjcheck(payload,r.length);if(pjget(payload+24,8)!=0)error("namespace: new directory must be empty");}else pjraw(payload,r.length);
            oscreatehandowner(tx,payload,r.length,&newid,up->owner?up->owner:ownerroot());
            if(r.operation==ProjectDetached){c->result=pjinstall(oh,newid,r.kind,rights,h->cap);goto done;}
        }else{
            other=vlookup(r.other,Vdelegate);oh2=pjbackend(other->obj,&newid);
            uchar a[16],b[16];osvolumeid(oh,a);osvolumeid(oh2,b);if(memcmp(a,b,16))error("namespace: cross volume");
            if(rights&~other->rights)error(Eperm);r.kind=other->obj->kind;
        }
        if(pjget(p+16,8)==~(uvlong)0)error("namespace: binding IDs exhausted");
        memset(add,0,sizeof add);pjput(add,pjget(p+16,8)+1,8);pjput(add+8,newid,8);
        pjput(add+16,rights,4);pjput(add+24,r.kind,4);addlen=strlen(r.name);pjput(add+28,addlen,4);memmove(add+32,r.name,addlen);addlen+=32;
        q=pjedit(p,n,-1,add,addlen,&qn);oswritehandk(tx,oid,q,qn);pjrefchange(tx,newid,1);oscommithand(tx,&ver);c->aux=ver;c->result=newid;goto done;
    }
    at=pjfind(p,raw,r.binding,nil);if(at<0)error("namespace: not found");
    if(r.operation==ProjectUnlink){
        q=pjedit(p,n,at,nil,0,&qn);oswritehandk(tx,oid,q,qn);pjrefchange(tx,pjget(p+at+8,8),-1);oscommithand(tx,&ver);c->aux=ver;goto done;
    }
    if(r.operation!=ProjectRename)error(Ebadarg);
    pjname(r.name);other=vlookup(r.other,Vcontrol);oh2=pjbackend(other->obj,&oid2);
    if(other->obj->kind!=VkindObjectSpace)error(Ebadarg);
    uchar a[16],b[16];osvolumeid(oh,a);osvolumeid(oh2,b);if(memcmp(a,b,16))error("namespace: cross volume");
    p2=pjload(oh2,oid2,&n2,1);raw2=pjraw(p2,n2);at2=pjfind(p2,raw2,0,r.name);
    if(at2>=0 && pjget(p2+at2+8,8)==pjget(p+at+8,8))goto done;
    if(at2>=0 && !r.replace)error("namespace: exists");
    if((pjget(p+at+16,4)&~other->rights)!=0)error(Eperm);
    memmove(add,p+at,32);addlen=strlen(r.name);pjput(add+28,addlen,4);memmove(add+32,r.name,addlen);addlen+=32;
    q=pjedit(p,n,at,nil,0,&qn);
    if(oid==oid2){
        if(at2>=0){long z=pjfind(q,pjraw(q,qn),pjget(p2+at2,8),nil);q2=pjedit(q,qn,z,nil,0,&q2n);free(q);q=q2;q2=nil;qn=q2n;}
        pjput(add,pjget(q+16,8)+1,8);q2=pjedit(q,qn,-1,add,addlen,&q2n);oswritehandk(tx,oid,q2,q2n);
    }else{
        pjput(add,pjget(p2+16,8)+1,8);q2=pjedit(p2,n2,at2,add,addlen,&q2n);
        oswritehandk(tx,oid,q,qn);oswritehandk(tx,oid2,q2,q2n);
    }
    if(at2>=0)pjrefchange(tx,pjget(p2+at2+8,8),-1);
    oscommithand(tx,&ver);c->aux=ver;
done:
    poperror();free(p);free(p2);free(q);free(q2);free(payload);oshandput(tx);vhandput(other);qunlock(&projectgate);
    poperror();vhandput(h);
}
