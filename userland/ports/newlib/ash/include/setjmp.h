/* POSIX mask preservation over genuine Newlib's AMD64 jmp_buf. The
 * unknown-elf Newlib headers expose only setjmp/longjmp, unlike Cygwin. */
#ifndef PLAN2001_POSIX_SETJMP_H
#define PLAN2001_POSIX_SETJMP_H
#include_next <setjmp.h>
#include <signal.h>
typedef struct {
    jmp_buf registers;
    sigset_t mask;
    int save_mask;
} P9SigJmp;
typedef P9SigJmp sigjmp_buf[1];
int p9_sigjmp_prepare(P9SigJmp *,int);
#define sigsetjmp(environment,save) \
    (p9_sigjmp_prepare(&(environment)[0],(save)),setjmp((environment)[0].registers))
_Noreturn void siglongjmp(sigjmp_buf,int);
#endif
