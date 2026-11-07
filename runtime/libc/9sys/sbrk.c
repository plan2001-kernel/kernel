#include <u.h>
#include <libc.h>

extern	char	end[];
static	char	*bloc = { end };

enum
{
	Round	= 7
};

/* The historical BRK_(24) kernel entry: ibrk(BSEG) with the same 8-byte
 * caller rounding, 0 return on success and errstr on failure. */
extern int brk_(void *);

int
brk(void *p)
{
	uintptr bl;

	bl = ((uintptr)p + Round) & ~Round;
	if(brk_((void*)bl) < 0)
		return -1;
	bloc = (char*)bl;
	return 0;
}

void*
sbrk(usize n)
{
	uintptr bl;

	bl = ((uintptr)bloc + Round) & ~Round;
	if(brk_((void*)(bl+n)) < 0)
		return (void*)-1;
	bloc = (char*)bl + n;
	return (void*)bl;
}
