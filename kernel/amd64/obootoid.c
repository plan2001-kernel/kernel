/*
 * The embedded first userspace loader for the native O server.
 *
 * The kernel supplies a pointer (in BP via otouser) to this argument block
 * in the user stack: the OID, the exact argv count and strings, the volume
 * spec and the native O Space name.  The loader uses only the existing user
 * O volume and OBJ Space syscalls; it has no pathname, FD, console or
 * writable global state.
 */
#include <u.h>
#include <libc.h>
#include "../../runtime/abi/obj.h"
#include "../../runtime/abi/sysctl.h"

enum {
	Maxargv	= 8,
};

struct Oargs
{
	uvlong	oid;
	long	argc;
	long	vollen;
	char	volspec[128];
	char	space[8];
	char	argv[Maxargv][128];
};

extern long sysctl(unsigned int, unsigned int, SysctlReq*, long);
extern long objopen(unsigned long long, void*, long, unsigned long long, void*);
extern long objclose(int);
extern long oexecute(int, uvlong, char**, int);

static long ovolopen(char *spec, int n);

/* The build encodes the loader entry as 0x200028, so _main must stay the
 * first emitted function. */
void
_main(struct Oargs *a)
{
	char *av[Maxargv+1];
	long h;
	int i;

	if(ovolopen(a->volspec, a->vollen) != 1)
		exits("ovolopen");
	h = objopen(ObjOpenSpace, a->space, 4, 0, nil);
	if(h < 0)
		exits("objopen");
	if(a->argc < 0 || a->argc > Maxargv)
		exits("oargc");
	for(i = 0; i < a->argc; i++)
		av[i] = a->argv[i];
	av[a->argc] = nil;
	oexecute(h, a->oid, av, a->argc);
	/* Failed load leaves ownership here; release the actual Space handle. */
	objclose(h);
	exits("oexecute");
}

/* The loader links only the generated syscall stubs, so it packs the SYSCTL
 * volume-activation record itself; there is no libc adapter here. */
static void
zmem(void *p, unsigned long n)
{
	unsigned char *b = p;

	while(n-- != 0)
		*b++ = 0;
}

static long
ovolopen(char *spec, int n)
{
	SysctlReq r;

	zmem(&r, sizeof r);
	r.version = SYSCTL_ABI_VERSION;
	r.data = spec;
	r.datalen = (unsigned long long)n;
	return sysctl(SysctlScopeSystem, SysctlVolumeOpen, &r, sizeof r);
}
