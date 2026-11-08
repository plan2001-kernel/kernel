/* GE client: persistent R1 operations and authorization are kernel-mediated. */
#include <string.h>
#include "py/runtime.h"
#include "py/objstr.h"
#include "py/objlist.h"
#ifndef NO_QSTR
#include "../runtime/ge_relations/relations.h"
#include "../runtime/abi/namespace.h"
#include "../runtime/abi/vobj.h"
#include "../runtime/abi/obj.h"
#endif
extern int p9_space_relation_handle(void),p9_space_relation_writer(void);
extern long oquery(int,void*,void*,int,void*);
static void qcheck(long rc) {
 if(rc<0)mp_raise_msg(&mp_type_OSError,MP_ERROR_TEXT("GE namespace authorization/query denied"));
}
static const char *text_arg(mp_obj_t value,size_t *n) {
 if(!mp_obj_is_str(value))mp_raise_TypeError(MP_ERROR_TEXT("ge: tag/query must be a string"));
 return mp_obj_str_get_data(value,n);
}
static uint64_t oid_arg(mp_obj_t value) {
 if(!mp_obj_is_int(value))mp_raise_TypeError(MP_ERROR_TEXT("ge: OID must be an integer"));
 uint64_t oid=(uint64_t)mp_obj_get_uint(value);
 if(oid<64||oid>=UINT64_C(0xffffffff00000000))mp_raise_ValueError(MP_ERROR_TEXT("ge: invalid OID"));
 return oid;
}
static NsIdentity identity_arg(mp_obj_t value) {
 NsIdentity id;memset(&id,0,sizeof id);
 if(mp_obj_is_int(value)){
  NsVisible visible[32];ObjCall call={0};
  long control=obj_open(ObjOpenNamespaceCurrent,NULL,0,Vcontrol,NULL);qcheck(control);
  call.version=OBJ_ABI_VERSION;call.op=ObjOpNamespaceVisible;
  call.out=visible;call.outlen=sizeof visible;
  long count=obj_invoke(control,&call,sizeof call);obj_close(control);qcheck(count);
  int found=0;
  for(size_t i=0;i<(size_t)count;i++)if(visible[i].object.oid==mp_obj_get_uint(value)){
   if(found)mp_raise_ValueError(MP_ERROR_TEXT("ambiguous OID; use qualified identity"));
   id=visible[i].object;found=1;
  }
  if(!found)qcheck(-1);
 }else{
  size_t n;mp_obj_t *a;mp_obj_get_array(value,&n,&a);
  if(n!=3)mp_raise_ValueError(MP_ERROR_TEXT("identity requires volume bytes, SpaceID, OID"));
  mp_buffer_info_t bytes;mp_get_buffer_raise(a[0],&bytes,MP_BUFFER_READ);
  if(bytes.len!=16)mp_raise_ValueError(MP_ERROR_TEXT("volume UUID length"));
  memcpy(id.volume,bytes.buf,16);id.space=mp_obj_get_uint(a[1]);id.oid=mp_obj_get_uint(a[2]);
 }
 return id;
}
static void *selector(mp_obj_t value,int *op,NsIdentity *id,NsAuthority *authority) {
 if(p9_space_relation_writer() && mp_obj_is_int(value)){
  authority->oid=oid_arg(value);authority->handle=p9_space_relation_handle();*op|=NS_BOOT;
  return authority;
 }
 *id=identity_arg(value);return id;
}
static mp_obj_t query_bytes(int op,void *id,const char *text,size_t len) {
 NsOutput out={0,0,0};qcheck(oquery(op,id,(void*)text,len,&out));
 if(out.count>GR_MAX_DATA)mp_raise_msg(&mp_type_MemoryError,MP_ERROR_TEXT("GE object limit"));
 vstr_t bytes;vstr_init_len(&bytes,out.count);
 out.buffer=(uintptr_t)bytes.buf;out.capacity=bytes.len;
 long rc=oquery(op,id,(void*)text,len,&out);
 if(rc<0){vstr_clear(&bytes);qcheck(rc);}
 bytes.len=out.count;return mp_obj_new_bytes_from_vstr(&bytes);
}
static mp_obj_t find_native(mp_obj_t query,int qualified) {
 size_t len;const char *q=text_arg(query,&len);int op=NS_FIND;
 NsAuthority authority;void *selection=NULL;
 if(!qualified && p9_space_relation_writer()){
  authority.handle=p9_space_relation_handle();authority.oid=0;selection=&authority;op|=NS_BOOT;
 }
 NsIdentity ids[GR_MAX_OBJECTS];NsOutput out={(uintptr_t)ids,sizeof ids,0};
 qcheck(oquery(op,selection,(void*)q,len,&out));
 mp_obj_t list=mp_obj_new_list(0,NULL);size_t n=out.count/sizeof ids[0];
 for(size_t i=0;i<n;i++){
  if(!qualified && i && (ids[i].space!=ids[0].space || memcmp(ids[i].volume,ids[0].volume,16)))
   mp_raise_ValueError(MP_ERROR_TEXT("multiple Spaces; use ge.find_ids"));
  mp_obj_t item=mp_obj_new_int_from_ull(ids[i].oid);
  if(qualified){mp_obj_t a[3]={mp_obj_new_bytes(ids[i].volume,16),mp_obj_new_int_from_ull(ids[i].space),item};item=mp_obj_new_tuple(3,a);}
  mp_obj_list_append(list,item);
 }
 return list;
}
static mp_obj_t ge_find(mp_obj_t query) {return find_native(query,0);}
static MP_DEFINE_CONST_FUN_OBJ_1(ge_find_obj,ge_find);
static mp_obj_t ge_find_ids(mp_obj_t query) {return find_native(query,1);}
static MP_DEFINE_CONST_FUN_OBJ_1(ge_find_ids_obj,ge_find_ids);
static mp_obj_t ge_create(mp_obj_t value) {
 mp_buffer_info_t bytes;
 if(mp_obj_is_str(value)||!mp_get_buffer(value,&bytes,MP_BUFFER_READ))
  mp_raise_TypeError(MP_ERROR_TEXT("ge.create: bytes-like data required"));
 /* Writer helper and the kernel independently reject ordinary children. */
 NsAuthority authority={p9_space_relation_handle(),0};uint64_t oid;
 NsOutput out={(uintptr_t)&oid,sizeof oid,0};
 qcheck(oquery(NS_CREATE|NS_BOOT,&authority,bytes.buf,bytes.len,&out));
 return mp_obj_new_int_from_ull(oid);
}
static MP_DEFINE_CONST_FUN_OBJ_1(ge_create_obj,ge_create);
static mp_obj_t ge_get(mp_obj_t value) {
 int op=NS_GET;NsIdentity id;NsAuthority authority;
 void *select=selector(value,&op,&id,&authority);return query_bytes(op,select,NULL,0);
}
static MP_DEFINE_CONST_FUN_OBJ_1(ge_get_obj,ge_get);
static mp_obj_t mutate(mp_obj_t value,mp_obj_t tag,int op) {
 NsIdentity id;NsAuthority authority;size_t n;const char *name=text_arg(tag,&n);
 void *select=selector(value,&op,&id,&authority);NsOutput out={0,0,0};
 qcheck(oquery(op,select,(void*)name,n,&out));return mp_const_none;
}
static mp_obj_t ge_add(mp_obj_t value,mp_obj_t tag) {return mutate(value,tag,NS_ADD);}
static MP_DEFINE_CONST_FUN_OBJ_2(ge_add_obj,ge_add);
static mp_obj_t ge_remove(mp_obj_t value,mp_obj_t tag) {return mutate(value,tag,NS_REMOVE);}
static MP_DEFINE_CONST_FUN_OBJ_2(ge_remove_obj,ge_remove);
static mp_obj_t ge_tags(mp_obj_t value) {
 int op=NS_TAGS;NsIdentity id;NsAuthority authority;
 void *select=selector(value,&op,&id,&authority);mp_obj_t raw=query_bytes(op,select,NULL,0);
 size_t n;const unsigned char *p=(const unsigned char*)mp_obj_str_get_data(raw,&n);
 mp_obj_t list=mp_obj_new_list(0,NULL);
 while(n){
  uint32_t len;if(n<4)qcheck(-1);memcpy(&len,p,4);p+=4;n-=4;
  if(len>n)qcheck(-1);
  mp_obj_list_append(list,mp_obj_new_str((const char*)p,len));p+=len;n-=len;
 }
 return list;
}
static MP_DEFINE_CONST_FUN_OBJ_1(ge_tags_obj,ge_tags);
static const mp_rom_map_elem_t ge_globals[] = {
 {MP_ROM_QSTR(MP_QSTR___name__),MP_ROM_QSTR(MP_QSTR_ge)},
 {MP_ROM_QSTR(MP_QSTR_create),MP_ROM_PTR(&ge_create_obj)},
 {MP_ROM_QSTR(MP_QSTR_get),MP_ROM_PTR(&ge_get_obj)},
 {MP_ROM_QSTR(MP_QSTR_add),MP_ROM_PTR(&ge_add_obj)},
 {MP_ROM_QSTR(MP_QSTR_remove),MP_ROM_PTR(&ge_remove_obj)},
 {MP_ROM_QSTR(MP_QSTR_tags),MP_ROM_PTR(&ge_tags_obj)},
 {MP_ROM_QSTR(MP_QSTR_find_ids),MP_ROM_PTR(&ge_find_ids_obj)},
 {MP_ROM_QSTR(MP_QSTR_find),MP_ROM_PTR(&ge_find_obj)},
};
static MP_DEFINE_CONST_DICT(ge_globals_dict,ge_globals);
const mp_obj_module_t mp_module_ge={.base={&mp_type_module},.globals=(mp_obj_dict_t*)&ge_globals_dict};
MP_REGISTER_MODULE(MP_QSTR_ge,mp_module_ge);
