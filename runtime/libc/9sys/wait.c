#include <u.h>
#include <libc.h>
#include "../../abi/obj.h"

/*
 * WAIT/AWAIT are typed OBJINVOKE(ObjOpWait) operations now.  The kernel
 * returns one bounded record (pid, user/sys/real time and the exact status
 * text); these adapters keep the historical libc APIs.  await() formats the
 * same five fields as before, while wait() allocates the historical
 * Waitmsg with a trailing NUL-terminated status string.
 */

int
await(char *buf, int n)
{
	ObjWait w;

	if(buf == nil || n <= 0)
		return -1;
	if(obj_wait(&w) < 0)
		return -1;
	return snprint(buf, n, "%d %lud %lud %lud %q",
		(int)w.pid,
		(ulong)w.time[0], (ulong)w.time[1], (ulong)w.time[2],
		w.msg);
}

Waitmsg*
wait(void)
{
	ObjWait w;
	Waitmsg *m;
	int l;

	if(obj_wait(&w) < 0)
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
