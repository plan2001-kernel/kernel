/*
 * libc adapters over SYSCTL(144).
 *
 * Environment, snapshot and volume-activation entry points keep their
 * historical libc signatures but have exactly one kernel route: SYSCTL.
 * The retired ENV*, SYSSNAPSHOT and OVOLOPEN syscall numbers stay reserved.
 */
#include	<u.h>
#include	<libc.h>
#include	<envapi.h>
#include	"../../abi/sysctl.h"

extern long sysctl(unsigned int, unsigned int, SysctlReq *, long);

long
envget(int nname, char *name, void *buf, long n, long *ulen)
{
	SysctlReq r;
	long ret;

	memset(&r, 0, sizeof r);
	r.version = SYSCTL_ABI_VERSION;
	r.name = name;
	r.namelen = (unsigned int)nname;
	r.data = buf;
	r.datalen = (unsigned long long)n;
	ret = sysctl(SysctlScopeOwnProcess, SysctlEnvGet, &r, sizeof r);
	if(ret < 0)
		return ret;
	if(ulen != nil)
		*ulen = (long)r.result;
	return 0;
}

long
envset(int nname, char *name, void *val, long n)
{
	SysctlReq r;

	memset(&r, 0, sizeof r);
	r.version = SYSCTL_ABI_VERSION;
	r.name = name;
	r.namelen = (unsigned int)nname;
	r.data = val;
	r.datalen = (unsigned long long)n;
	return sysctl(SysctlScopeOwnProcess, SysctlEnvSet, &r, sizeof r);
}

long
envdelete(int nname, char *name)
{
	SysctlReq r;

	memset(&r, 0, sizeof r);
	r.version = SYSCTL_ABI_VERSION;
	r.name = name;
	r.namelen = (unsigned int)nname;
	return sysctl(SysctlScopeOwnProcess, SysctlEnvDelete, &r, sizeof r);
}

long
envlist(void *buf, long n, long *need)
{
	SysctlReq r;
	long ret;

	memset(&r, 0, sizeof r);
	r.version = SYSCTL_ABI_VERSION;
	r.data = buf;
	r.datalen = (unsigned long long)n;
	ret = sysctl(SysctlScopeOwnProcess, SysctlEnvList, &r, sizeof r);
	if(ret < 0)
		return ret;
	if(need != nil)
		*need = (long)r.result;
	return 0;
}

long
syssnapshot(PlanSystemSnapshot *v)
{
	SysctlReq r;

	if(v == nil)
		return -1;
	memset(&r, 0, sizeof r);
	r.version = SYSCTL_ABI_VERSION;
	r.data = v;
	r.datalen = sizeof *v;
	return sysctl(SysctlScopeSystem, SysctlSnapshot, &r, sizeof r);
}

long
ovolopen(char *spec, int n)
{
	SysctlReq r;

	if(spec == nil || n <= 0)
		return -1;
	memset(&r, 0, sizeof r);
	r.version = SYSCTL_ABI_VERSION;
	r.data = spec;
	r.datalen = (unsigned long long)n;
	return sysctl(SysctlScopeSystem, SysctlVolumeOpen, &r, sizeof r);
}

long
proc_info(SysctlProcInfo *out)
{
	SysctlReq r;

	if(out == nil)
		return -1;
	memset(out, 0, sizeof *out);
	memset(&r, 0, sizeof r);
	r.version = SYSCTL_ABI_VERSION;
	r.data = out;
	r.datalen = sizeof *out;
	return sysctl(SysctlScopeOwnProcess, SysctlProcessInfo, &r, sizeof r);
}
