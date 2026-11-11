/*
 * SYSCTL(144): typed system/process configuration and introspection.
 *
 * One entry point owns system snapshot, own-process Handle-list listing,
 * process environment operations, privileged volume activation and
 * own-process CPU/memory introspection.  It does not transfer object data,
 * create or wait for children, mutate namespace bindings or issue device
 * commands.  Authority comes from the explicit scope, the caller's own
 * process, or the existing boot-owner check for volume activation.
 */
#include	"u.h"
#include	"../core/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../core/error.h"
#include	"vobjint.h"
#include	"../../runtime/abi/sys.h"
#include	"../../runtime/abi/vobj.h"
#include	"../../runtime/abi/sysctl.h"
#include	"owner.h"

static void
sysctlbuf(void *p, uvlong n, int write)
{
	if(n == 0)
		return;
	if(p == nil || n > 0x7fffffffULL)
		error(Ebadarg);
	validaddr((uintptr)p, (uintptr)n, write);
}

static int
scopeok(unsigned int scope)
{
	return scope == SysctlScopeOwnProcess || scope == SysctlScopeSystem;
}

uintptr
syssysctl(sysva_list list)
{
	SysctlReq r, ur;
	SysctlProcInfo pi;
	unsigned int scope, op;
	void *req;
	long size, n;
	uvlong cap, total;

	scope = (unsigned int)sysva_arg(list, uvlong);
	op = (unsigned int)sysva_arg(list, uvlong);
	req = sysva_arg(list, void*);
	size = sysva_arg(list, long);
	if(!scopeok(scope))
		error(Ebadarg);
	if(req == nil || size != sizeof ur)
		error(Ebadarg);
	validaddr((uintptr)req, sizeof ur, 1);
	memmove(&ur, req, sizeof ur);
	if(ur.version != SYSCTL_ABI_VERSION || ur.flags != 0 ||
	   ur.reserved != 0 || ur.arg0 != 0 || ur.arg1 != 0)
		error(Ebadarg);
	r = ur;
	r.result = 0;
	r.aux = 0;

	switch(op){
	case SysctlSnapshot:
		if(scope != SysctlScopeSystem || r.name != nil || r.namelen != 0)
			error(Ebadarg);
		sysctlbuf(r.data, r.datalen, 1);
		r.result = sysctlsnapshot(r.data, r.datalen);
		break;
	case SysctlHandleList:
		if(scope != SysctlScopeOwnProcess || r.name != nil ||
		   r.namelen != 0 || r.datalen % sizeof(VobjListRec) != 0)
			error(Ebadarg);
		cap = r.datalen / sizeof(VobjListRec);
		if(cap > Vhandmax)
			error(Ebadarg);
		sysctlbuf(r.data, r.datalen, 1);
		total = (uvlong)vobjlist(r.data, (long)cap);
		r.result = (long long)total;
		r.aux = total < cap ? total : cap;
		break;
	case SysctlEnvGet:
		if(scope != SysctlScopeOwnProcess || r.name == nil ||
		   r.namelen == 0 || r.namelen >= 128)
			error(Ebadarg);
		if(r.datalen > 0x7fffffffULL)
			error(Ebadarg);
		n = 0;
		if(r.data != nil && r.datalen > 0)
			sysctlbuf(r.data, r.datalen, 1);
		sysctlenvget(r.name, (int)r.namelen, r.data, (long)r.datalen,
			&n);
		r.result = n;
		r.aux = (r.data != nil && r.datalen > 0) ? (uvlong)n : 0;
		break;
	case SysctlEnvSet:
		if(scope != SysctlScopeOwnProcess || r.name == nil ||
		   r.namelen == 0 || r.namelen >= 128)
			error(Ebadarg);
		if(r.datalen > 0x7fffffffULL)
			error(Ebadarg);
		sysctlbuf(r.data, r.datalen, 0);
		sysctlenvset(r.name, (int)r.namelen, r.data, (long)r.datalen);
		break;
	case SysctlEnvDelete:
		if(scope != SysctlScopeOwnProcess || r.name == nil ||
		   r.namelen == 0 || r.namelen >= 128 || r.data != nil ||
		   r.datalen != 0)
			error(Ebadarg);
		sysctlenvdelete(r.name, (int)r.namelen);
		break;
	case SysctlEnvList:
		if(scope != SysctlScopeOwnProcess || r.name != nil ||
		   r.namelen != 0)
			error(Ebadarg);
		if(r.datalen > 0x7fffffffULL)
			error(Ebadarg);
		if(r.data != nil && r.datalen > 0)
			sysctlbuf(r.data, r.datalen, 1);
		n = 0;
		sysctlenvlist(r.data, (long)r.datalen, &n);
		r.result = n;
		r.aux = (r.data != nil && r.datalen > 0) ? (uvlong)n : 0;
		break;
	case SysctlVolumeOpen:
		if(scope != SysctlScopeSystem || r.name != nil ||
		   r.namelen != 0 || r.data == nil || r.datalen == 0 ||
		   r.datalen > 256)
			error(Ebadarg);
		sysctlbuf(r.data, r.datalen, 0);
		r.result = osvolumeopen(r.data, (int)r.datalen);
		break;
	case SysctlProcessInfo:
		if(scope != SysctlScopeOwnProcess || r.name != nil ||
		   r.namelen != 0 || r.datalen != sizeof pi || r.data == nil)
			error(Ebadarg);
		sysctlbuf(r.data, r.datalen, 1);
		memset(&pi, 0, sizeof pi);
		pi.pid = up->pid;
		pi.cpu = up->affinity < 0 ? ~0ULL : (uvlong)up->affinity;
		lock(&up->memlock);
		pi.membudget = up->membudget;
		pi.owner = up->owner;
		unlock(&up->memlock);
		memmove(r.data, &pi, sizeof pi);
		break;
	case SysctlOwnerInfo:
	{
		SysctlOwnerInfoRec oi;

		if(scope != SysctlScopeOwnProcess || r.name != nil ||
		   r.namelen != 0 || r.datalen != sizeof oi || r.data == nil)
			error(Ebadarg);
		sysctlbuf(r.data, r.datalen, 1);
		memset(&oi, 0, sizeof oi);
		oi.version = SYSCTL_ABI_VERSION;
		oi.owner = up->owner;
		ownerinfo(up->owner, &oi.flags, oi.name, sizeof oi.name);
		memmove(r.data, &oi, sizeof oi);
		break;
	}
	default:
		error(Ebadarg);
	}

	memmove(req, &r, sizeof r);
	return (uintptr)r.result;
}
