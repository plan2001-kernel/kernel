#include	"u.h"
#include	"../core/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../core/error.h"

/*
 * Typed observations of the current process.  These are kernel C operations,
 * not a handle to an arbitrary process and not a userspace ABI.  -1/nil/0
 * report the absence of a current process where the return type allows it.
 */
int
procpid(void)
{
	if(up == nil)
		return -1;
	return up->pid;
}

int
procppid(void)
{
	if(up == nil)
		return -1;
	return up->parentpid;
}

char*
procusername(void)
{
	if(up == nil)
		return nil;
	return up->user;
}

/*
 * Six inherited accounting slots, in the Proc.time order (user, sys, real,
 * child user, child sys, child real), converted to milliseconds.  TReal is
 * elapsed time since the process started, matching the legacy text read.
 */
int
proccputimes(uvlong *v, int n)
{
	int i;

	if(up == nil || n <= 0)
		return 0;
	if(n > 6)
		n = 6;
	for(i = 0; i < n; i++){
		if(i == TReal)
			v[i] = tk2ms(MACHP(0)->pmach.ticks - up->time[i]);
		else
			v[i] = tk2ms(up->time[i]);
	}
	return n;
}
