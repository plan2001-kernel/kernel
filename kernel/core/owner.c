/*
 * Single-owner identity registry and trusted registered-program launch.
 *
 * OwnerID is a stable 64-bit identity derived from a trusted name, not a
 * Unix uid/gid.  The registry is created by boot/installation code and is
 * not writable through object rights, delegation or namespace bindings.
 * Registered programs bind a name, owner, owner-owned executable object and
 * startup policy.  Launching one starts an independent process under the
 * program's owner; the requester receives observation only.
 */
#include	"u.h"
#include	"../core/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../core/error.h"
#include	"../../runtime/abi/sys.h"
#include	"../../runtime/abi/vobj.h"
#include	"../../runtime/abi/obj.h"
#include	"vobjint.h"
#include	"../../ge/storage/ocontext.h"
#include	"owner.h"

enum {
	OwnerHashOff	= 1469598103934665603ULL,
	OwnerHashPrime	= 1099511628211ULL,
	OwnerProgramBytes = 8*1024*1024,
};

typedef struct OwnerEnt OwnerEnt;
struct OwnerEnt {
	uvlong	id;
	char	name[OwnerNameMax];
};

typedef struct OwnerProgram OwnerProgram;
struct OwnerProgram {
	char	name[OwnerProgramNameMax];
	uvlong	owner;
	Ohand	*space;
	uvlong	oid;
	int	policy;
};

static Lock	ownerlk;
static OwnerEnt	owners[OwnerMax];
static int	nowners;
static int	ownerinited;

static Lock	proglk;
static OwnerProgram programs[OwnerProgramMax];
static int	nprograms;

static uvlong	bootprogramoid;

static uvlong
ownerhash(char *name, int n)
{
	uvlong h;
	int i;

	h = OwnerHashOff;
	for(i = 0; i < n; i++){
		h ^= (uchar)name[i];
		h *= OwnerHashPrime;
	}
	if(h == 0 || h == 1)
		h ^= 0x9e3779b97f4a7c15ULL;
	return h;
}

uvlong
ownerroot(void)
{
	static uvlong root;

	if(root == 0)
		root = ownerhash("root", 4);
	return root;
}

void
ownerinit(void)
{
	if(ownerinited)
		return;
	ownerinited = 1;
	ownercreate("root", 4);
}

int
ownernamed(uvlong id, char *name, int n)
{
	int i;

	if(name == nil || n <= 0)
		return 0;
	if(id == 0){
		snprint(name, n, "kernel");
		return 1;
	}
	lock(&ownerlk);
	for(i = 0; i < nowners; i++)
		if(owners[i].id == id){
			snprint(name, n, "%s", owners[i].name);
			unlock(&ownerlk);
			return 1;
		}
	unlock(&ownerlk);
	name[0] = 0;
	return 0;
}

uvlong
ownercreate(char *name, int n)
{
	uvlong id;
	int i;

	if(name == nil || n <= 0 || n >= OwnerNameMax)
		error(Ebadarg);
	for(i = 0; i < n; i++)
		if(name[i] == 0)
			error(Ebadarg);
	id = ownerhash(name, n);
	lock(&ownerlk);
	for(i = 0; i < nowners; i++){
		if(owners[i].id == id){
			if(strlen(owners[i].name) == (int)n &&
			   memcmp(owners[i].name, name, n) == 0){
				unlock(&ownerlk);
				return id;
			}
			unlock(&ownerlk);
			error("owner: identity collision");
		}
	}
	if(nowners >= OwnerMax){
		unlock(&ownerlk);
		error("owner: registry full");
	}
	owners[nowners].id = id;
	memmove(owners[nowners].name, name, n);
	owners[nowners].name[n] = 0;
	nowners++;
	unlock(&ownerlk);
	return id;
}

int
ownerinfo(uvlong id, unsigned int *flags, char *name, int n)
{
	int ok;

	if(flags != nil)
		*flags = 0;
	ok = ownernamed(id, name, n);
	if(ok && flags != nil)
		*flags = 1;
	return ok;
}

/*
 * Boot places the effective executable OID here before activating the O
 * volume.  osvolumeopen() then records the default autostart entry.  The
 * loader's in-place exec remains the actual autostart action; the registry
 * entry makes the same program launchable by name.
 */
int
ownerprogrambootprepare(uvlong oid)
{
	bootprogramoid = oid;
	return 0;
}

void
ownerprogrambootregister(void)
{
	OwnerProgram *p;
	Ohand *soh;

	if(bootprogramoid == 0 || up == nil || !up->boot)
		return;
	if(waserror()){
		print("owner: boot program registration failed: %s\n", up->errstr);
		return;
	}
	soh = osopenspacek("init", 4);
	if(waserror()){
		oshandput(soh);
		nexterror();
	}
	{
		Ohand *del = oscopycapdelegate(soh, Oexecute);

		oshandput(soh);
		soh = del;
	}
	lock(&proglk);
	if(nprograms >= OwnerProgramMax){
		unlock(&proglk);
		error("owner: program registry full");
	}
	p = &programs[nprograms++];
	memset(p, 0, sizeof *p);
	strcpy(p->name, "boot");
	p->owner = ownerroot();
	p->space = soh;
	p->oid = bootprogramoid;
	p->policy = OwnerProgramAutostart | OwnerProgramLaunchable;
	unlock(&proglk);
	poperror();
	poperror();
}

uintptr
sysprogramregister(sysva_list list)
{
	ProgramRegisterReq r;
	ProgramRegisterReq *ureq;
	uchar *buf;
	Ohand *soh;
	uvlong owner, oid, ver;
	long n, size;
	unsigned int policy;
	int i;

	ureq = sysva_arg(list, ProgramRegisterReq*);
	size = sysva_arg(list, long);
	if(ureq == nil || size != sizeof r)
		error(Ebadarg);
	validaddr((uintptr)ureq, sizeof r, 1);
	memmove(&r, ureq, sizeof r);
	if(r.version != OWNER_PROGRAM_ABI_VERSION || r.reserved != 0)
		error(Ebadarg);
	policy = r.policy;
	if(policy == 0 || (policy & ~(unsigned int)(OwnerProgramAutostart|
	    OwnerProgramLaunchable)) != 0)
		error(Ebadarg);
	if(r.namelen == 0 || r.namelen >= OwnerProgramNameMax ||
	   r.ownerlen == 0 || r.ownerlen >= OwnerNameMax)
		error(Ebadarg);
	for(i = 0; i < (int)r.namelen; i++)
		if(r.name[i] == 0)
			error(Ebadarg);
	for(i = 0; i < (int)r.ownerlen; i++)
		if(r.owner_name[i] == 0)
			error(Ebadarg);
	/* Registration is a trusted boot/installation action, never a normal
	 * process operation that could forge an owner. */
	if(up == nil || !up->boot)
		error(Eperm);
	lock(&proglk);
	for(i = 0; i < nprograms; i++)
		if(strlen(programs[i].name) == (int)r.namelen &&
		   memcmp(programs[i].name, r.name, r.namelen) == 0){
			unlock(&proglk);
			error("owner: program already registered");
		}
	unlock(&proglk);

	owner = ownercreate(r.owner_name, (int)r.ownerlen);
	soh = vobjholdohand(r.space, VkindSpace, Vexecute);
	buf = nil;
	if(waserror()){
		free(buf);
		oshandput(soh);
		nexterror();
	}
	n = osgetobjecthand(soh, r.oid, &buf, OwnerProgramBytes, &ver);
	if(n <= 0)
		error("program: empty executable");
	oscreatehandowner(soh, buf, n, &oid, owner);
	{
		Ohand *del = oscopycapdelegate(soh, Oexecute);

		oshandput(soh);
		soh = del;
	}
	poperror();

	lock(&proglk);
	if(nprograms >= OwnerProgramMax){
		unlock(&proglk);
		oshandput(soh);
		error("owner: program registry full");
	}
	{
		OwnerProgram *p = &programs[nprograms++];

		memset(p, 0, sizeof *p);
		memmove(p->name, r.name, r.namelen);
		p->name[r.namelen] = 0;
		p->owner = owner;
		p->space = soh;
		p->oid = oid;
		p->policy = (int)policy;
	}
	unlock(&proglk);
	free(buf);

	r.oid = oid;
	r.owner = owner;
	memmove(ureq, &r, sizeof r);
	return 0;
}

uintptr
sysprogramlaunch(sysva_list list)
{
	char *name;
	long namelen;
	char **argv;
	int argc, i, h, obs;
	struct Ospawn *opt;
	OwnerProgram *p;
	char pname[OwnerProgramNameMax];

	name = sysva_arg(list, char*);
	namelen = sysva_arg(list, long);
	argv = sysva_arg(list, char**);
	argc = (int)sysva_arg(list, int);
	opt = sysva_arg(list, struct Ospawn*);
	if(name == nil || namelen <= 0 || namelen >= OwnerProgramNameMax)
		error(Ebadarg);
	validaddr((uintptr)name, (uintptr)namelen, 0);
	memmove(pname, name, namelen);
	pname[namelen] = 0;
	for(i = 0; i < namelen; i++)
		if(pname[i] == 0)
			error(Ebadarg);

	lock(&proglk);
	for(p = nil, i = 0; i < nprograms; i++)
		if(strlen(programs[i].name) == (int)namelen &&
		   memcmp(programs[i].name, pname, namelen) == 0){
			p = &programs[i];
			break;
		}
	if(p == nil || (p->policy & OwnerProgramLaunchable) == 0){
		unlock(&proglk);
		error("program: not registered or not launchable");
	}
	unlock(&proglk);

	h = kernospawn(p->space, p->oid, argv, argc, opt, p->owner, 1);
	if(waserror()){
		vobjclosehandle(h);
		nexterror();
	}
	obs = vobjopenhandle(h, Vobserve|Vmeta|Vdelegate, nil);
	poperror();
	vobjclosehandle(h);
	return (uintptr)obs;
}
