#include	"u.h"
#include	"../core/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../core/error.h"

extern uchar	configfile[];

char*
sysnameget(void)
{
	return sysname;
}

void
sysnameset(char *s)
{
	kstrdup(&sysname, s);
}

char*
hostownerget(void)
{
	return eve;
}

char*
hostdomainget(void)
{
	return hostdomain;
}

char*
configget(void)
{
	return (char*)configfile;
}

char*
osversionget(void)
{
	return "2000";
}

/*
 * Privileged clock/control boundary.  Kernel callers with no process
 * context are trusted; a process-context caller must be the host owner.
 * This preserves the inherited iseve policy without dereferencing eve when
 * an O-only boot has no user namespace.
 */
int
timecontrolallowed(void)
{
	if(up == nil)
		return 1;
	if(eve == nil || up->user == nil)
		return 0;
	return strcmp(eve, up->user) == 0;
}

int
sysstatget(int id, Sysstat *v)
{
	Mach *mp;
	ulong l;

	if(id < 0 || id >= MAXMACH || !active.machs[id])
		return 0;
	mp = MACHP(id);
	v->id = id;
	v->cs = mp->pmach.cs;
	v->intr = mp->pmach.intr;
	v->syscall = mp->pmach.syscall;
	v->pfault = mp->pmach.pfault;
	v->tlbfault = mp->pmach.tlbfault;
	v->tlbpurge = mp->pmach.tlbpurge;
	v->load = mp->pmach.load;
	l = mp->pmach.perf.period;
	if(l == 0)
		l = 1;
	v->idle = (mp->pmach.perf.avg_inidle*100)/l;
	v->inintr = (mp->pmach.perf.avg_inintr*100)/l;
	return 1;
}

void
sysstatreset(void)
{
	int id;
	Mach *mp;

	for(id = 0; id < MAXMACH; id++) {
		if(!active.machs[id])
			continue;
		mp = MACHP(id);
		mp->pmach.cs = 0;
		mp->pmach.intr = 0;
		mp->pmach.syscall = 0;
		mp->pmach.pfault = 0;
		mp->pmach.tlbfault = 0;
		mp->pmach.tlbpurge = 0;
	}
}

/* /dev/null write semantics, without a file or Qid. */
long
nulldiscard(void *va, long n)
{
	(void)va;
	return n;
}

/* /dev/zero read semantics, without a file or Qid. */
long
zerofill(void *va, long n)
{
	memset(va, 0, n);
	return n;
}

/* UJ1 fixed read-only status snapshot: version, CPUs, physical/user bytes,
 * native O partition capacity. No kernel pointers or approximate free space.
 * Word 5 reports the live Procstat completion-record count.
 * SYSCTL SysctlSnapshot: the caller supplies exactly seven uvlong words. */
extern uvlong ovolcapacity(void);
long
sysctlsnapshot(void *user, uvlong n)
{
 uvlong v[7];

 if(user == nil || n != sizeof v)
  error(Ebadarg);
 validaddr((uintptr)user,sizeof v,1);
#ifdef PLAN2001_SYSCALL_TRACE
 extern void syscalltracedump(char*);
 syscalltracedump("snapshot");
#endif
 v[0]=1;v[1]=conf.nmach;v[2]=(uvlong)conf.npage*BY2PG;
 v[3]=(uvlong)conf.upages*BY2PG;v[4]=ovolcapacity();v[5]=(uvlong)procstatlive();
 lock(&palloc.lk);v[6]=(uvlong)palloc.freecount*BY2PG;unlock(&palloc.lk);
 memmove(user,v,sizeof v);return 0;
}
