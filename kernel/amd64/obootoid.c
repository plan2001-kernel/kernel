/*
 * The embedded first userspace loader for the native O server.
 *
 * The kernel supplies a pointer (in BP via otouser) to this argument block
 * in the user stack: the OID, the exact argv count and strings, the volume
 * spec and the native O Space name.  The loader uses only the existing user
 * native-O syscalls; it has no pathname, FD, console or writable global
 * state.
 */
#include <u.h>
#include <libc.h>

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

extern long ovolopen(char*, int);
extern long ospaceopen(int, char*, int);
extern long ospaceclose(int);
extern long oexecute(int, uvlong, char**, int);

void
_main(struct Oargs *a)
{
	char *av[Maxargv+1];
	long h;
	int i;

	if(ovolopen(a->volspec, a->vollen) != 1)
		exits("ovolopen");
	h = ospaceopen(1, a->space, 4);
	if(h < 0)
		exits("ospaceopen");
	if(a->argc < 0 || a->argc > Maxargv)
		exits("oargc");
	for(i = 0; i < a->argc; i++)
		av[i] = a->argv[i];
	av[a->argc] = nil;
	oexecute(h, a->oid, av, a->argc);
	/* Failed load leaves ownership here; release the actual Space handle. */
	ospaceclose(h);
	exits("oexecute");
}
