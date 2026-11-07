#ifndef PLAN2001_ESPCONTROL_H
#define PLAN2001_ESPCONTROL_H
#include "esp6.h"
#include "../../runtime/abi/espctl.h"
typedef struct EspControl EspControl;
struct EspControl {
    Esp6Table table;
    uint64_t generation, slotgen[ESPCTL_PEERS];
    uint64_t installed, removed, stale, invalid;
    uint8_t local[16], identity[64];
    unsigned identityn;
};
typedef int (*EspEntropy)(void *,uint8_t *,size_t);
int espcontrol_exec(EspControl *,unsigned,const uint8_t *,size_t,uint8_t *,size_t *,uint64_t,EspEntropy,void *);
void espcontrol_expire(EspControl *,uint64_t);
void espcontrol_disconnect(EspControl *);
#endif
