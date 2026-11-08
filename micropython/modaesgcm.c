/* In-process AES-128-GCM binding; no syscall or kernel ESP service.
 * Reuses the existing allocation-free BearSSL wrapper, not new crypto. */
#include "py/runtime.h"
#include "py/objstr.h"
#include <string.h>
#ifndef NO_QSTR
#include "../runtime/ikecrypto/ikecrypto.h"
#endif

static mp_obj_t
gcm_crypt(const mp_obj_t *args, int encrypt)
{
    mp_buffer_info_t key, nonce, aad, input;
    unsigned char combined[20], tag[16];
    size_t n;
    int ok;
    vstr_t out;

    mp_get_buffer_raise(args[0], &key, MP_BUFFER_READ);
    mp_get_buffer_raise(args[1], &nonce, MP_BUFFER_READ);
    mp_get_buffer_raise(args[2], &aad, MP_BUFFER_READ);
    mp_get_buffer_raise(args[3], &input, MP_BUFFER_READ);
    if(key.len != 16 || nonce.len != 12 || aad.len > 64
    || input.len > 2064 || (!encrypt && input.len < 16)
    || (encrypt && input.len > 2048))
        mp_raise_ValueError(MP_ERROR_TEXT("AES-GCM buffer bounds"));
    n = input.len - (encrypt ? 0 : 16);
    vstr_init_len(&out, n + (encrypt ? 16 : 0));
    memcpy(combined, key.buf, 16);
    memcpy(combined+16, nonce.buf, 4);
    if(n) memcpy(out.buf, input.buf, n);
    if(!encrypt) memcpy(tag, (unsigned char*)input.buf+n, 16);
    ok = ike_gcm(encrypt, combined, (unsigned char*)nonce.buf+4,
        aad.buf, aad.len, (unsigned char*)out.buf, n, tag);
    ike_wipe(combined, sizeof combined);
    if(encrypt) memcpy(out.buf+n, tag, 16);
    ike_wipe(tag, sizeof tag);
    if(!ok){
        vstr_clear(&out); /* ike_gcm wiped unauthenticated plaintext */
        mp_raise_ValueError(MP_ERROR_TEXT("AES-GCM tag mismatch"));
    }
    return mp_obj_new_bytes_from_vstr(&out);
}

static mp_obj_t gcm_encrypt(size_t n_args, const mp_obj_t *args) { return gcm_crypt(args, 1); }
static mp_obj_t gcm_decrypt(size_t n_args, const mp_obj_t *args) { return gcm_crypt(args, 0); }
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(encrypt_obj, 4, 4, gcm_encrypt);
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(decrypt_obj, 4, 4, gcm_decrypt);

static const mp_rom_map_elem_t globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_aesgcm) },
    { MP_ROM_QSTR(MP_QSTR_encrypt), MP_ROM_PTR(&encrypt_obj) },
    { MP_ROM_QSTR(MP_QSTR_decrypt), MP_ROM_PTR(&decrypt_obj) },
};
static MP_DEFINE_CONST_DICT(globals, globals_table);
const mp_obj_module_t aesgcm_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t*)&globals,
};
MP_REGISTER_MODULE(MP_QSTR_aesgcm, aesgcm_module);
