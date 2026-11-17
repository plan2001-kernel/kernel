/* Single-process, synchronous Linux host adapter for native GE/O sources. */
#include <u.h>
#include <libc.h>
#undef assert
#define assert(x) do {if(!(x))_assert(#x);} while(0)
#include <fcall.h>
#include <avl.h>
typedef void Proc;
typedef struct Gefs Gefs;
extern Proc *up;
#include "gdat.h"
typedef struct Errctx Errctx;
struct Errctx {long tid;char err[128];jmp_buf errlab[Estacksz];int nerrlab;};
extern Errctx **errctx;
jmp_buf *_waserror(void);
_Noreturn void error(char *,...);
_Noreturn void nexterror(void);
#define waserror() (setjmp(*_waserror()))
#define poperror() assert((*errctx)->nerrlab-- > 0)
#define errmsg() ((*errctx)->err)
#define estacksz() ((*errctx)->nerrlab)
#define print(...) fprint(2,__VA_ARGS__)
#define cachedel(a) gcachedel(fs,(a))
void gflushdev(Gefs *);
void splhi(void);
void yield(void);
#define Ebadarg "invalid argument"
#include "gfns.h"

int oobjownerget(void *,uvlong,uvlong *,int *);
