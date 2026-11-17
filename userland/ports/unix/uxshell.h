/* Small native Unix command evaluator; NOT upstream BusyBox ash.
 * Redirection and bounded, single-producer pipelines live in userland.
 */
#ifndef PLAN2001_UXSHELL_H
#define PLAN2001_UXSHELL_H
#include "uxfs.h"
typedef struct {
    void *context;
    int (*emit)(void *, const void *, unsigned int);
    /* Optional separate-process executor; results must be collected from
     * explicitly delegated descriptor regions (never from raw child PIDs). */
    int (*external)(void *, const char *, char *const *, unsigned int,
                    void *, unsigned int, unsigned int *);
    int external_ls;
    /* Optional real SPAWN + UXPIPE two-process pipeline backend. */
    int (*pipeline)(void *, char *const *, unsigned int,
                    char *const *, unsigned int,
                    void *, unsigned int, unsigned int *);
} UxShellIO;
int uxshell_eval(UxFs *fs, const char *line, const UxShellIO *io);
#endif
