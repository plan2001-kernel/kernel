#include <string.h>
#include "py/runtime.h"
#include "py/compile.h"
#include "py/lexer.h"
#include "py/parse.h"
#include "py/persistentcode.h"
#include "py/objmodule.h"
#include "py/objstr.h"
#include "namespace_source.h"
#include "posix_source.h"
MP_REGISTER_ROOT_POINTER(mp_obj_t posix_module);
MP_REGISTER_ROOT_POINTER(mp_obj_t namespace_module);
static void run_source(const char *src,size_t size,qstr filename)
{
    mp_lexer_t *lex=mp_lexer_new_from_str_len(filename,src,size,0);
    mp_parse_tree_t tree=mp_parse(lex,MP_PARSE_FILE_INPUT);
    mp_call_function_0(mp_compile(&tree,filename,false));
}
mp_obj_t p9_namespace_module(void)
{
    if(MP_STATE_VM(namespace_module)!=MP_OBJ_NULL)return MP_STATE_VM(namespace_module);
    mp_obj_t mod=mp_obj_new_module(MP_QSTR_namespace);
    mp_obj_dict_t *g=((mp_obj_module_t*)MP_OBJ_TO_PTR(mod))->globals;
    nlr_jump_callback_node_globals_locals_t saved;
    saved.globals=mp_globals_get();saved.locals=mp_locals_get();
    mp_globals_set(g);mp_locals_set(g);
    nlr_push_jump_callback(&saved.callback,mp_globals_locals_set_from_nlr_jump_callback);
    run_source(namespace_source,sizeof(namespace_source)-1,MP_QSTR_namespace);
    nlr_pop_jump_callback(true);MP_STATE_VM(namespace_module)=mod;return mod;
}
mp_obj_t p9_posix_module(void)
{
    if(MP_STATE_VM(posix_module)!=MP_OBJ_NULL)return MP_STATE_VM(posix_module);
    mp_obj_t mod=mp_obj_new_module(MP_QSTR_posix);
    mp_obj_dict_t *g=((mp_obj_module_t*)MP_OBJ_TO_PTR(mod))->globals;
    nlr_jump_callback_node_globals_locals_t saved;
    saved.globals=mp_globals_get();saved.locals=mp_locals_get();
    mp_globals_set(g);mp_locals_set(g);
    nlr_push_jump_callback(&saved.callback,mp_globals_locals_set_from_nlr_jump_callback);
    run_source(posix_source,sizeof(posix_source)-1,MP_QSTR_posix);
    nlr_pop_jump_callback(true);MP_STATE_VM(posix_module)=mod;return mod;
}
mp_obj_t p9_namespace_import(size_t n,const mp_obj_t *a)
{
    if(MP_STATE_VM(namespace_module)==MP_OBJ_NULL)return mp_const_none;
    mp_obj_t fn=mp_load_attr(MP_STATE_VM(namespace_module),MP_QSTR__import);
    return mp_call_function_n_kw(fn,n,0,a);
}
mp_obj_t p9_project_new_module(void)
{
    mp_module_context_t *ctx=m_new_obj(mp_module_context_t);memset(ctx,0,sizeof *ctx);
    ctx->module.base.type=&mp_type_module;ctx->module.globals=MP_OBJ_TO_PTR(mp_obj_new_dict(8));
    return MP_OBJ_FROM_PTR(&ctx->module);
}
mp_obj_t p9_project_exec(mp_obj_t module,mp_obj_t source,mp_obj_t ismpy)
{
    mp_module_context_t *ctx=MP_OBJ_TO_PTR(module);
    mp_buffer_info_t b;mp_get_buffer_raise(source,&b,MP_BUFFER_READ);
    nlr_jump_callback_node_globals_locals_t saved;
    saved.globals=mp_globals_get();saved.locals=mp_locals_get();
    mp_globals_set(ctx->module.globals);mp_locals_set(ctx->module.globals);
    nlr_push_jump_callback(&saved.callback,mp_globals_locals_set_from_nlr_jump_callback);
    if(mp_obj_is_true(ismpy)){
        mp_compiled_module_t cm={0};cm.context=ctx;mp_raw_code_load_mem(b.buf,b.len,&cm);
        mp_call_function_0(mp_make_function_from_proto_fun(cm.rc,cm.context,NULL));
    }else run_source(b.buf,b.len,MP_QSTR_namespace);
    nlr_pop_jump_callback(true);return mp_const_none;
}
