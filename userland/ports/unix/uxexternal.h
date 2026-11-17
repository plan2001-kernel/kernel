/* Optional native external applet executor for a granted Unix view.
 * The launch authority and executable OID are supplied by an explicit
 * higher-privileged userland bootstrap; paths NEVER mint GE/O authority.
 */
#ifndef PLAN2001_UXEXTERNAL_H
#define PLAN2001_UXEXTERNAL_H
#include "uxservice.h"
typedef struct {
    UxFs *fs;
    UxService *service;         /* shared authoritative UXFS/GE-O owner */
    int executable_volume;          /* authorized Vexecute GE/O Handle */
    UxOid executable_oid;           /* identity only */
} UxExternal;
int uxexternal_pipeline(void *ctx, char *const *left, unsigned int lcount,
                        char *const *right, unsigned int rcount,
                        void *output, unsigned int capacity,
                        unsigned int *size);
int uxexternal_ls(void *ctx, const char *command, char *const *argv,
                  unsigned int argc, void *output, unsigned int capacity,
                  unsigned int *size);
#endif
