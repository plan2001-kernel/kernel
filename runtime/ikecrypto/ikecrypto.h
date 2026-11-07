#ifndef IKECRYPTO_H
#define IKECRYPTO_H
#include "ikeport.h"
void ike_wipe(void *, size_t);
int ike_equal(const void *, const void *, size_t);
void ike_hash(const void *, size_t, uint8_t[32]);
void ike_mac(const void *, size_t, const void *, size_t,
    const void *, size_t, const void *, size_t, uint8_t[32]);
int ike_scalar(const uint8_t[32]);
void ike_public(const uint8_t[32], uint8_t[64]);
int ike_shared(const uint8_t[32], const uint8_t[64], uint8_t[32]);
int ike_gcm(int, const uint8_t[20], const uint8_t[8], const void *, size_t,
    uint8_t *, size_t, uint8_t[16]);
#endif
