#include "oenv.h"
#include <string.h>
static const unsigned char domain[]="Plan2001-O O-Envelope v1";
static int fieldtype(const CbDoc *d,const char *name,unsigned type,size_t size){
 unsigned id=cb_field(d,name);if(!id)return 0;
 const CbValue *v=&d->values[id-1];
 return v->type==type&&(!size||v->len==size)?0:CB_INVALID;
}
int oe_validate(const CbDoc *d){
 if(!d||!d->root||d->root>d->used||d->used>CB_MAX_NODES||d->values[d->root-1].type!=CB_MAP)return CB_INVALID;
 unsigned v=cb_field(d,"v");if(!v||d->values[v-1].type!=CB_UINT||d->values[v-1].integer!=1)return CB_INVALID;
 if(fieldtype(d,"name",CB_TEXT,0)||fieldtype(d,"data",CB_BYTES,0)||fieldtype(d,"created",CB_TEXT,0)||
 fieldtype(d,"media_type",CB_TEXT,0)||fieldtype(d,"metadata",CB_MAP,0)||fieldtype(d,"parts",CB_ARRAY,0)||
 fieldtype(d,"previous",CB_BYTES,32)||fieldtype(d,"sha256",CB_BYTES,32))return CB_INVALID;
 return 0;
}
static int addfield(CbDoc *d,const char *key,unsigned type,const void *data,size_t n){
 if(d->used>CB_MAX_NODES-2)return CB_LIMIT;
 unsigned k=cb_new(d,CB_TEXT),v=cb_new(d,type);
 d->values[k-1].bytes=(const unsigned char*)key;d->values[k-1].len=strlen(key);
 d->values[v-1].bytes=data;d->values[v-1].len=n;
 if(type==CB_UINT)d->values[v-1].integer=1;
 return cb_map_add(d,d->root,k,v);
}
int oe_pack(CbDoc *d,void *out,size_t cap,size_t *n,unsigned char digest[32],OeHash hash){
 if(!d||!out||!n||!digest||!hash||!d->root||d->root>d->used||d->used>CB_MAX_NODES||d->values[d->root-1].type!=CB_MAP)return CB_INVALID;
 if(!cb_field(d,"v")){int rc=addfield(d,"v",CB_UINT,NULL,0);if(rc)return rc;}
 int rc=oe_validate(d);if(rc)return rc;
 rc=cb_encode_without(d,"sha256",out,cap,n);if(rc)return rc;
 if(hash(domain,sizeof domain,out,*n,digest))return CB_INVALID;
 unsigned id=cb_field(d,"sha256");
 if(id){d->values[id-1].bytes=digest;d->values[id-1].len=32;}
 else {rc=addfield(d,"sha256",CB_BYTES,digest,32);if(rc)return rc;}
 return cb_encode(d,out,cap,n);
}
int oe_unpack(CbDoc *d,const void *in,size_t len,void *scratch,size_t cap,int required,OeHash hash){
 if(!scratch||!hash)return CB_INVALID;
 int rc=cb_decode(d,in,len);if(rc)return rc;
 rc=oe_validate(d);if(rc)return rc;
 unsigned id=cb_field(d,"sha256");
 if(!id)return required?CB_INTEGRITY:0;
 size_t n;unsigned char digest[32];
 rc=cb_encode_without(d,"sha256",scratch,cap,&n);if(rc)return rc;
 if(hash(domain,sizeof domain,scratch,n,digest))return CB_INVALID;
 unsigned diff=0;for(unsigned i=0;i<32;i++)diff|=digest[i]^d->values[id-1].bytes[i];
 memset(digest,0,sizeof digest);return diff?CB_INTEGRITY:0;
}
