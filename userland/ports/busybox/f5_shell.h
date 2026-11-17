#ifndef PLAN2001_F5_SHELL_H
#define PLAN2001_F5_SHELL_H
#include "../unix/uxfs.h"
typedef struct {
    int volume;               /* explicitly delegated authorized GE/O Handle */
    UxOid manifest;           /* existing, caller-provisioned manifest OID */
    int restore;              /* 1: manifest must already exist and validate */
    int executable_volume;    /* explicit authorized SPAWN Vexecute Handle */
    UxOid executable_oid;     /* validated executable identity, NOT authority */
} UxF5Storage;

/* Bounded F5 interpreter. NOT upstream BusyBox ash/sh. A non-NULL
 * context may point to UxF5Storage to opt into persistent GE/O I/O
 * and independently the external ls process. Without a grant: no SPAWN.
 */
int p2001_f5_shell(Pvd *view, void *context);
#endif
