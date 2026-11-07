/* Small allocation-free boundary around the vendored BearSSL primitives. */
#include "bearssl/inc/bearssl.h"
#ifndef PLAN2001_GCC
#include "ikecrypto.h"
#endif
#include <string.h>

void
ike_wipe(void *p, size_t n)
{
    volatile uint8_t *q = p;
    while(n--) *q++ = 0;
}

int
ike_equal(const void *a, const void *b, size_t n)
{
    const uint8_t *x = a, *y = b;
    unsigned diff = 0;
    while(n--) diff |= *x++ ^ *y++;
    return diff == 0;
}

int
ike_gcm(int encrypt, const uint8_t key[20], const uint8_t iv[8],
    const void *aad, size_t an, uint8_t *data, size_t n, uint8_t tag[16])
{
    br_aes_ct_ctr_keys aes;
    br_gcm_context g;
    uint8_t nonce[12];
    int ok=1;
    memcpy(nonce, key+16, 4); memcpy(nonce+4, iv, 8);
    br_aes_ct_ctr_init(&aes, key, 16);
    br_gcm_init(&g, &aes.vtable, br_ghash_ctmul);
    br_gcm_reset(&g, nonce, sizeof nonce);
    if(an) br_gcm_aad_inject(&g, aad, an);
    br_gcm_flip(&g);
    br_gcm_run(&g, encrypt, data, n);
    if(encrypt) br_gcm_get_tag(&g, tag);
    else if(!(ok = br_gcm_check_tag(&g, tag))) ike_wipe(data, n);
    ike_wipe(&g, sizeof g); ike_wipe(&aes, sizeof aes);
    ike_wipe(nonce, sizeof nonce);
    return ok;
}
