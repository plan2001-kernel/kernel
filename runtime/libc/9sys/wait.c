#include <u.h>
#include <libc.h>
#include "../../abi/obj.h"

/*
 * The no-argument wait()/await() interface is userland bookkeeping over
 * VkindProcess Handles created by this process's spawn/rforkex adapters.
 * Completion is multi-reader and non-consumptive in the kernel; this adapter
 * closes its tracking Handle after copying the result, matching the
 * historical "collect one child" ergonomics.  New code should use
 * obj_process_wait() directly so it can observe completion repeatedly and
 * keep the Handle.
 */

extern long objprocwaitany(ObjProcessInfo *out);

int
await(char *buf, int n)
{
	ObjProcessInfo w;

	if(buf == nil || n <= 0)
		return -1;
	if(objprocwaitany(&w) < 0)
		return -1;
	return snprint(buf, n, "%d %lud %lud %lud %q",
		(int)w.pid,
		(ulong)w.time[0], (ulong)w.time[1], (ulong)w.time[2],
		w.msg);
}

Waitmsg*
wait(void)
{
	ObjProcessInfo w;
	Waitmsg *m;
	int l;

	if(objprocwaitany(&w) < 0)
		return nil;
	l = w.msglen + 1;
	m = malloc(sizeof(Waitmsg) + l);
	if(m == nil)
		return nil;
	m->pid = (int)w.pid;
	m->time[0] = (ulong)w.time[0];
	m->time[1] = (ulong)w.time[1];
	m->time[2] = (ulong)w.time[2];
	m->msg = (char*)&m[1];
	memmove(m->msg, w.msg, w.msglen);
	m->msg[w.msglen] = '\0';
	return m;
}
