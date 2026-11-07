#include "cbor.h"
#include <string.h>
void cb_init(CbDoc *d){memset(d,0,sizeof *d);}
unsigned cb_new(CbDoc *d,unsigned type){
 if(!d||d->used>=CB_MAX_NODES||type>CB_NULL)return 0;
 unsigned id=++d->used;memset(&d->values[id-1],0,sizeof d->values[0]);
 d->values[id-1].type=type;if(!d->root)d->root=id;return id;
}
static const CbValue *node(const CbDoc *d,unsigned id){return id&&id<=d->used&&d->used<=CB_MAX_NODES?&d->values[id-1]:NULL;}
static int scalar(const CbValue *v){return v&&v->type!=CB_ARRAY&&v->type!=CB_MAP&&v->type<=CB_NULL;}
static int equal(const CbValue *a,const CbValue *b){
 if(!a||!b||a->type!=b->type)return 0;
 if(a->type==CB_TEXT||a->type==CB_BYTES)return a->len==b->len&&(!a->len||!memcmp(a->bytes,b->bytes,a->len));
 return a->type==CB_NULL||a->integer==b->integer;
}
static int append(CbDoc *d,unsigned parent,unsigned child){
 CbValue *p=&d->values[parent-1];unsigned *link=&p->child,steps=0;
 while(*link){if(!node(d,*link)||++steps>CB_MAX_NODES)return CB_INVALID;link=&d->values[*link-1].next;}
 *link=child;return 0;
}
int cb_array_add(CbDoc *d,unsigned parent,unsigned child){
 const CbValue *p=node(d,parent),*c=node(d,child);
 if(!p||p->type!=CB_ARRAY||!c||c->next||parent==child)return CB_INVALID;
 int rc=append(d,parent,child);if(!rc)d->values[parent-1].len++;return rc;
}
int cb_map_add(CbDoc *d,unsigned parent,unsigned key,unsigned val){
 const CbValue *p=node(d,parent),*k=node(d,key),*v=node(d,val);
 if(!p||p->type!=CB_MAP||!scalar(k)||!v||k->next||v->next||parent==key||parent==val||key==val)return CB_INVALID;
 unsigned i=p->child,steps=0;
 while(i){const CbValue *old=node(d,i);if(!old||++steps>CB_MAX_NODES)return CB_INVALID;
  if(equal(old,k))return CB_DUPLICATE;
  old=node(d,old->next);if(!old)return CB_INVALID;i=old->next;
 }
 int rc=append(d,parent,key);if(rc)return rc;
 d->values[key-1].next=val;d->values[parent-1].len++;return 0;
}
int cb_utf8(const unsigned char *s,size_t n){
 if(!s&&n)return 0;
 for(size_t i=0;i<n;){unsigned c=s[i++],count;uint32_t v,min;
  if(c<128)continue;
  if(c>=0xc2&&c<=0xdf){count=1;v=c&31;min=0x80;}
  else if(c>=0xe0&&c<=0xef){count=2;v=c&15;min=0x800;}
  else if(c>=0xf0&&c<=0xf4){count=3;v=c&7;min=0x10000;}else return 0;
  if(count>n-i)return 0;
  while(count--){c=s[i++];if((c&0xc0)!=0x80)return 0;v=(v<<6)|(c&63);}
  if(v<min||v>0x10ffff||(v>=0xd800&&v<=0xdfff))return 0;
 }return 1;
}
typedef struct {const unsigned char *p;size_t n,pos;CbDoc *d;} Reader;
static int argument(Reader *r,unsigned ai,uint64_t *v){
 if(ai<24){*v=ai;return 0;}
 if(ai>27)return ai==31?CB_UNSUPPORTED:CB_INVALID;
 unsigned n=1u<<(ai-24);if(n>r->n-r->pos)return CB_TRUNCATED;
 *v=0;while(n--)*v=(*v<<8)|r->p[r->pos++];return 0;
}
static int readvalue(Reader *r,unsigned depth,unsigned *out){
 if(depth>CB_MAX_DEPTH)return CB_LIMIT;
 if(r->pos==r->n)return CB_TRUNCATED;
 unsigned byte=r->p[r->pos++],major=byte>>5,ai=byte&31,type;uint64_t arg=0;
 if(major==6||major==7){
  if(major==6||ai<20||ai>22)return CB_UNSUPPORTED;
  type=ai==22?CB_NULL:CB_BOOL;arg=ai==21;
 }else{
  int rc=argument(r,ai,&arg);if(rc)return rc;
  type=major==0?CB_UINT:major==1?CB_NEG:major==2?CB_BYTES:major==3?CB_TEXT:major==4?CB_ARRAY:CB_MAP;
 }
 unsigned id=cb_new(r->d,type);if(!id)return CB_LIMIT;
 CbValue *v=&r->d->values[id-1];v->integer=arg;*out=id;
 if(type==CB_BYTES||type==CB_TEXT){
  if(arg>r->n-r->pos)return CB_TRUNCATED;
  v->len=(size_t)arg;v->bytes=r->p+r->pos;r->pos+=v->len;
  if(type==CB_TEXT&&!cb_utf8(v->bytes,v->len))return CB_UTF8;
 }else if(type==CB_ARRAY||type==CB_MAP){
  if(arg>CB_MAX_NODES/(type==CB_MAP?2:1))return CB_LIMIT;
  for(uint64_t i=0;i<arg;i++){
   unsigned a,b;int rc=readvalue(r,depth+1,&a);if(rc)return rc;
   if(type==CB_MAP){
    if(!scalar(node(r->d,a)))return CB_UNSUPPORTED;
    rc=readvalue(r,depth+1,&b);if(!rc)rc=cb_map_add(r->d,id,a,b);
   }else rc=cb_array_add(r->d,id,a);
   if(rc)return rc;
  }
 }
 return 0;
}
int cb_decode(CbDoc *d,const void *bytes,size_t n){
 if(!d||(!bytes&&n))return CB_INVALID;
 cb_init(d);if(n>CB_MAX_BYTES)return CB_LIMIT;
 Reader r={bytes,n,0,d};unsigned root;int rc=readvalue(&r,0,&root);
 if(!rc&&r.pos!=n)rc=CB_INVALID;
 if(rc){cb_init(d);return rc;}d->root=root;return 0;
}
static unsigned head(unsigned char *p,unsigned major,uint64_t v){
 if(v<24){p[0]=(unsigned char)((major<<5)|v);return 1;}
 unsigned n=v<=0xff?1:v<=0xffff?2:v<=0xffffffff?4:8;
 p[0]=(unsigned char)((major<<5)|(n==1?24:n==2?25:n==4?26:27));
 for(unsigned i=0;i<n;i++)p[n-i]=(unsigned char)(v>>(8*i));return n+1;
}
static unsigned keyhead(const CbValue *v,unsigned char *p){
 if(v->type==CB_BOOL){p[0]=v->integer?0xf5:0xf4;return 1;}
 if(v->type==CB_NULL){p[0]=0xf6;return 1;}
 return head(p,v->type,v->type==CB_BYTES||v->type==CB_TEXT?v->len:v->integer);
}
static int keycompare(const CbValue *a,const CbValue *b){
 unsigned char ah[9],bh[9];unsigned an=keyhead(a,ah),bn=keyhead(b,bh),n=an<bn?an:bn;
 int rc=memcmp(ah,bh,n);if(rc)return rc;
 if(an!=bn)return an<bn?-1:1;
 if(a->type==CB_BYTES||a->type==CB_TEXT){
  /* lengths equal because the complete encoded heads match */
  return a->len?memcmp(a->bytes,b->bytes,a->len):0;
 }return 0;
}
typedef struct {unsigned char *p;size_t cap,n;unsigned visits;const CbDoc *d;const char *omit;} Writer;
static int put(Writer *w,const void *p,size_t n){
 if(n>CB_MAX_BYTES-w->n)return CB_LIMIT;
 if(w->p){if(n>w->cap-w->n)return CB_LIMIT;if(n)memcpy(w->p+w->n,p,n);}
 w->n+=n;return 0;
}
static int omitted(const CbValue *k,const char *name){return name&&k->type==CB_TEXT&&k->len==strlen(name)&&!memcmp(k->bytes,name,k->len);}
static int writevalue(Writer *w,unsigned id,unsigned depth){
 const CbValue *v=node(w->d,id);if(!v)return CB_INVALID;
 if(depth>CB_MAX_DEPTH||++w->visits>CB_MAX_NODES)return CB_LIMIT;
 if(v->type>CB_NULL)return CB_UNSUPPORTED;
 if((v->type==CB_BYTES||v->type==CB_TEXT)&&((!v->bytes&&v->len)||v->len>CB_MAX_BYTES))return CB_INVALID;
 if(v->type==CB_TEXT&&!cb_utf8(v->bytes,v->len))return CB_UTF8;
 if(v->type==CB_BOOL&&v->integer>1)return CB_INVALID;
 unsigned char h[9];unsigned n;
 if(v->type==CB_MAP||v->type==CB_ARRAY){
  unsigned keys[CB_MAX_NODES/2],count=0,child=v->child;int rc;
  if(v->len>CB_MAX_NODES/(v->type==CB_MAP?2:1))return CB_LIMIT;
  if(v->type==CB_MAP){
   for(size_t i=0;i<v->len;i++){
    const CbValue *k=node(w->d,child);if(!scalar(k))return CB_UNSUPPORTED;
    if((k->type==CB_TEXT||k->type==CB_BYTES)&&((!k->bytes&&k->len)||k->len>CB_MAX_BYTES))return CB_INVALID;
    if(k->type==CB_TEXT&&!cb_utf8(k->bytes,k->len))return CB_UTF8;
    if(k->type==CB_BOOL&&k->integer>1)return CB_INVALID;
    const CbValue *value=node(w->d,k->next);if(!value)return CB_INVALID;
    for(unsigned j=0;j<count;j++)if(equal(k,node(w->d,keys[j])))return CB_DUPLICATE;
    /* Include omitted keys in duplicate checking as well. */
    unsigned at=count;while(at&&keycompare(k,node(w->d,keys[at-1]))<0){keys[at]=keys[at-1];at--;}
    keys[at]=child;count++;child=value->next;
   }
   if(child)return CB_INVALID;
   unsigned kept=count;
   for(unsigned i=0;i<count;i++)if(depth==0&&omitted(node(w->d,keys[i]),w->omit))kept--;
   n=head(h,5,kept);rc=put(w,h,n);if(rc)return rc;
   for(unsigned i=0;i<count;i++){
    const CbValue *k=node(w->d,keys[i]);
    if(depth==0&&omitted(k,w->omit))continue;
    rc=writevalue(w,keys[i],depth+1);if(!rc)rc=writevalue(w,k->next,depth+1);if(rc)return rc;
   }
  }else{
   n=head(h,4,v->len);rc=put(w,h,n);if(rc)return rc;
   for(size_t i=0;i<v->len;i++){const CbValue *c=node(w->d,child);if(!c)return CB_INVALID;
    rc=writevalue(w,child,depth+1);if(rc)return rc;child=c->next;}
   if(child)return CB_INVALID;
  }
  return 0;
 }
 n=keyhead(v,h);int rc=put(w,h,n);if(rc)return rc;
 return v->type==CB_BYTES||v->type==CB_TEXT?put(w,v->bytes,v->len):0;
}
int cb_encode_without(const CbDoc *d,const char *omit,void *out,size_t cap,size_t *n){
 if(!d||!n||(!out&&cap))return CB_INVALID;
 Writer w={out,cap,0,0,d,omit};int rc=writevalue(&w,d->root,0);if(!rc)*n=w.n;return rc;
}
int cb_encode(const CbDoc *d,void *out,size_t cap,size_t *n){return cb_encode_without(d,NULL,out,cap,n);}
unsigned cb_field(const CbDoc *d,const char *name){
 const CbValue *root=node(d,d->root);if(!root||root->type!=CB_MAP)return 0;
 unsigned child=root->child,steps=0;
 while(child){const CbValue *k=node(d,child);if(!k||++steps>CB_MAX_NODES)return 0;
  const CbValue *v=node(d,k->next);if(!v)return 0;
  if(omitted(k,name))return k->next;child=v->next;
 }return 0;
}
