#ifndef PLAN2001_ESP6_H
#define PLAN2001_ESP6_H
#include "../../runtime/ikecrypto/ikeport.h"
#define ESP6_PEERS 16
#define ESP6_PACKET 1500
/* One host-to-host IPv6 policy per provisioned identity. Keep the policy
 * after deleting keys: a closed/expired SA must never fall back to plaintext. */
typedef struct Esp6SA Esp6SA;
typedef struct Esp6Table Esp6Table;
struct Esp6SA {
    int policy, active, transport;
    uint8_t local[16], remote[16], rxkey[20], txkey[20];
    uint32_t rxspi, txspi, txseq, rxhigh;
    uint64_t rxwindow, expires, last;
};
struct Esp6Table {
    Esp6SA sa[ESP6_PEERS];
    uint64_t accepted, sent, replay, badtag, rejected;
};
void esp6_clear(Esp6SA *);
int esp6_management(const uint8_t *, size_t);
int esp6_plain_forbidden(Esp6Table *, const uint8_t *, size_t);
int esp6_seal(Esp6Table *, const uint8_t *, size_t, uint8_t[ESP6_PACKET], uint64_t);
int esp6_open(Esp6Table *, const uint8_t *, size_t, uint8_t[ESP6_PACKET], uint64_t);
#endif
