/* Optional CBOR/O-Envelope application codecs; never open or authorize GE. */
#include <string.h>
#include "py/runtime.h"
#include "py/objstr.h"
#include "py/objint.h"
#ifndef NO_QSTR
#include "../runtime/cbor/oenv.h"
#endif
extern int p9sha256parts(const void *,size_t,const void *,size_t,unsigned char*);
static void check(int rc){
 if(rc==CB_OK)return;
 if(rc==CB_LIMIT)mp_raise_msg(&mp_type_MemoryError,MP_ERROR_TEXT("CBOR resource limit"));
 mp_raise_msg_varg(&mp_type_ValueError,MP_ERROR_TEXT("CBOR/O-Envelope error %d"),rc);
}
static unsigned frompy(CbDoc *d,mp_obj_t obj,unsigned depth){
 if(depth>CB_MAX_DEPTH)check(CB_LIMIT);
 unsigned type;
 if(obj==mp_const_none)type=CB_NULL;
 else if(obj==mp_const_true||obj==mp_const_false)type=CB_BOOL;
 else if(mp_obj_is_int(obj))type=mp_obj_is_true(mp_binary_op(MP_BINARY_OP_LESS,obj,MP_OBJ_NEW_SMALL_INT(0)))?CB_NEG:CB_UINT;
 else if(mp_obj_is_str(obj))type=CB_TEXT;
 else if(mp_obj_is_type(obj,&mp_type_bytes))type=CB_BYTES;
 else if(mp_obj_is_type(obj,&mp_type_list)||mp_obj_is_type(obj,&mp_type_tuple))type=CB_ARRAY;
 else if(mp_obj_is_type(obj,&mp_type_dict))type=CB_MAP;
 else {check(CB_UNSUPPORTED);return 0;}
 unsigned id=cb_new(d,type);if(!id)check(CB_LIMIT);
 CbValue *v=&d->values[id-1];
 if(type==CB_UINT||type==CB_NEG){
  mp_obj_t value=type==CB_NEG?mp_unary_op(MP_UNARY_OP_INVERT,obj):obj;
  if(mp_obj_is_true(mp_binary_op(MP_BINARY_OP_MORE,value,mp_obj_new_int_from_ull(UINT64_MAX))))check(CB_OVERFLOW);
  /* mp_obj_get_int truncates large values; int.to_bytes performs a checked
   * full-width conversion without assuming signed machine-int range. */
  byte bytes[8];
  mp_obj_t args[4],method[2];
  mp_load_method(value,MP_QSTR_to_bytes,method);
  args[0]=method[0];args[1]=method[1];args[2]=MP_OBJ_NEW_SMALL_INT(8);args[3]=MP_OBJ_NEW_QSTR(MP_QSTR_big);
  mp_obj_t encoded=mp_call_method_n_kw(2,0,args);mp_buffer_info_t info;
  mp_get_buffer_raise(encoded,&info,MP_BUFFER_READ);memcpy(bytes,info.buf,8);
  for(unsigned i=0;i<8;i++)v->integer=(v->integer<<8)|bytes[i];
 }else if(type==CB_BOOL)v->integer=obj==mp_const_true;
 else if(type==CB_TEXT||type==CB_BYTES){
  size_t n;v->bytes=(const unsigned char*)mp_obj_str_get_data(obj,&n);v->len=n;
 }else if(type==CB_ARRAY){
  size_t n;mp_obj_t *items;mp_obj_get_array(obj,&n,&items);
  if(n>CB_MAX_NODES)check(CB_LIMIT);
  for(size_t i=0;i<n;i++){unsigned child=frompy(d,items[i],depth+1);check(cb_array_add(d,id,child));}
 }else if(type==CB_MAP){
  mp_map_t *map=mp_obj_dict_get_map(obj);
  if(map->used>CB_MAX_NODES/2)check(CB_LIMIT);
  for(size_t i=0;i<map->alloc;i++)if(mp_map_slot_is_filled(map,i)){
   unsigned k=frompy(d,map->table[i].key,depth+1),val=frompy(d,map->table[i].value,depth+1);
   check(cb_map_add(d,id,k,val));
  }
 }
 return id;
}
static mp_obj_t topy(const CbDoc *d,unsigned id,unsigned depth){
 if(depth>CB_MAX_DEPTH||!id||id>d->used)check(CB_INVALID);
 const CbValue *v=&d->values[id-1];
 switch(v->type){
 case CB_UINT:return mp_obj_new_int_from_ull(v->integer);
 case CB_NEG:return mp_unary_op(MP_UNARY_OP_INVERT,mp_obj_new_int_from_ull(v->integer));
 case CB_BOOL:return mp_obj_new_bool(v->integer);
 case CB_NULL:return mp_const_none;
 case CB_BYTES:return mp_obj_new_bytes(v->bytes,v->len);
 case CB_TEXT:return mp_obj_new_str((const char*)v->bytes,v->len);
 default:break;
 }
 mp_obj_t result=v->type==CB_MAP?mp_obj_new_dict(v->len):mp_obj_new_list(0,NULL);
 unsigned child=v->child;
 for(size_t i=0;i<v->len;i++){
  mp_obj_t item=topy(d,child,depth+1);child=d->values[child-1].next;
  if(v->type==CB_MAP){
   /* CBOR true and integer 1 differ; Python dictionaries equate them.
    * Reject the conversion rather than silently drop a CBOR map entry. */
   if(mp_map_lookup(mp_obj_dict_get_map(result),item,MP_MAP_LOOKUP))check(CB_DUPLICATE);
   mp_obj_dict_store(result,item,topy(d,child,depth+1));child=d->values[child-1].next;
  }else mp_obj_list_append(result,item);
 }
 return result;
}
static mp_obj_t cbor_dumps(mp_obj_t obj){
 CbDoc *d=m_new_obj(CbDoc);cb_init(d);d->root=frompy(d,obj,0);
 size_t n;check(cb_encode(d,NULL,0,&n));vstr_t out;vstr_init_len(&out,n);
 check(cb_encode(d,out.buf,n,&n));m_del_obj(CbDoc,d);return mp_obj_new_bytes_from_vstr(&out);
}
static MP_DEFINE_CONST_FUN_OBJ_1(cbor_dumps_obj,cbor_dumps);
static mp_obj_t cbor_loads(mp_obj_t obj){
 mp_buffer_info_t bytes;mp_get_buffer_raise(obj,&bytes,MP_BUFFER_READ);
 CbDoc *d=m_new_obj(CbDoc);check(cb_decode(d,bytes.buf,bytes.len));
 mp_obj_t out=topy(d,d->root,0);m_del_obj(CbDoc,d);return out;
}
static MP_DEFINE_CONST_FUN_OBJ_1(cbor_loads_obj,cbor_loads);
static mp_obj_t oenv_pack(mp_obj_t obj){
 CbDoc *d=m_new_obj(CbDoc);cb_init(d);d->root=frompy(d,obj,0);
 /* Worst-case new v/sha256 fields: 3 + 41 bytes. Encode measures and
  * validates the source first, before reserving a bounded output. */
 size_t n;check(cb_encode(d,NULL,0,&n));if(n>CB_MAX_BYTES-48)check(CB_LIMIT);
 vstr_t out;vstr_init_len(&out,n+48);unsigned char digest[32];
 check(oe_pack(d,out.buf,out.len,&n,digest,p9sha256parts));out.len=n;
 m_del_obj(CbDoc,d);return mp_obj_new_bytes_from_vstr(&out);
}
static MP_DEFINE_CONST_FUN_OBJ_1(oenv_pack_obj,oenv_pack);
static mp_obj_t oenv_unpack(size_t n,const mp_obj_t *args){
 mp_buffer_info_t bytes;mp_get_buffer_raise(args[0],&bytes,MP_BUFFER_READ);
 if(bytes.len>CB_MAX_BYTES)check(CB_LIMIT);
 CbDoc *d=m_new_obj(CbDoc);byte *scratch=m_new(byte,bytes.len?bytes.len:1);
 check(oe_unpack(d,bytes.buf,bytes.len,scratch,bytes.len,n>1&&mp_obj_is_true(args[1]),p9sha256parts));
 mp_obj_t result=topy(d,d->root,0);m_del(byte,scratch,bytes.len?bytes.len:1);m_del_obj(CbDoc,d);return result;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(oenv_unpack_obj,1,2,oenv_unpack);
static const mp_rom_map_elem_t cbor_globals[]={
 {MP_ROM_QSTR(MP_QSTR___name__),MP_ROM_QSTR(MP_QSTR_cbor)},
 {MP_ROM_QSTR(MP_QSTR_dumps),MP_ROM_PTR(&cbor_dumps_obj)},
 {MP_ROM_QSTR(MP_QSTR_loads),MP_ROM_PTR(&cbor_loads_obj)},
};
static MP_DEFINE_CONST_DICT(cbor_globals_dict,cbor_globals);
const mp_obj_module_t mp_module_cbor={.base={&mp_type_module},.globals=(mp_obj_dict_t*)&cbor_globals_dict};
MP_REGISTER_MODULE(MP_QSTR_cbor,mp_module_cbor);
static const mp_rom_map_elem_t oenv_globals[]={
 {MP_ROM_QSTR(MP_QSTR___name__),MP_ROM_QSTR(MP_QSTR_oenv)},
 {MP_ROM_QSTR(MP_QSTR_pack),MP_ROM_PTR(&oenv_pack_obj)},
 {MP_ROM_QSTR(MP_QSTR_unpack),MP_ROM_PTR(&oenv_unpack_obj)},
};
static MP_DEFINE_CONST_DICT(oenv_globals_dict,oenv_globals);
const mp_obj_module_t mp_module_oenv={.base={&mp_type_module},.globals=(mp_obj_dict_t*)&oenv_globals_dict};
MP_REGISTER_MODULE(MP_QSTR_oenv,mp_module_oenv);
