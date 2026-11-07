#include	"u.h"
#include	"../core/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"../core/error.h"
#include	"../amd64/rtcapi.h"
#include	"env/envapi.h"

enum
{
	Envnamemax	= 128,
	Envvalmax	= 512*1024,
	Envlistmax	= 1*1024*1024,
};

static char*
envnamecopy(char *u, int n)
{
	char *k;

	if(u == nil || n <= 0 || n >= Envnamemax)
		error(Ebadarg);
	if(!okaddr((uintptr)u, n, 0))
		error(Ebadarg);
	k = smalloc(n+1);
	memmove(k, u, n);
	k[n] = 0;
	if(memchr(k, 0, n) != nil){
		free(k);
		error(Ebadarg);
	}
	return k;
}

uintptr
sysrtcget(sysva_list list)
{
	Rtcdate *u, d;

	u = sysva_arg(list, Rtcdate*);
	if(u == nil || !okaddr((uintptr)u, sizeof d, 1))
		error(Ebadarg);
	if(rtcnativeget(&d) < 0)
		error(up->errstr[0] != 0 ? up->errstr : Eio);
	memmove(u, &d, sizeof d);
	return 0;
}

uintptr
sysrtcset(sysva_list list)
{
	Rtcdate *u, d;

	u = sysva_arg(list, Rtcdate*);
	if(u == nil || !okaddr((uintptr)u, sizeof d, 0))
		error(Ebadarg);
	memmove(&d, u, sizeof d);
	if(rtcnativeset(&d) < 0)
		error(up->errstr[0] != 0 ? up->errstr : Eperm);
	return 0;
}

uintptr
sysenvget(sysva_list list)
{
	int nname;
	char *uname, *name, *kb;
	void *buf;
	long n, vlen, *ulen;

	nname = sysva_arg(list, int);
	uname = sysva_arg(list, char*);
	buf = sysva_arg(list, void*);
	n = sysva_arg(list, long);
	ulen = sysva_arg(list, long*);
	if(n < 0 || n > Envvalmax)
		error(Ebadarg);
	if(ulen != nil && !okaddr((uintptr)ulen, sizeof(long), 1))
		error(Ebadarg);
	name = envnamecopy(uname, nname);
	if(waserror()){
		free(name);
		nexterror();
	}
	if(buf != nil && n > 0 && !okaddr((uintptr)buf, n, 1))
		error(Ebadarg);
	vlen = 0;
	if(envnativeget(name, nil, 0, &vlen) < 0)
		error(up->errstr[0] != 0 ? up->errstr : Eio);
	if(buf == nil || n == 0){
		if(ulen != nil)
			memmove(ulen, &vlen, sizeof vlen);
		free(name);
		poperror();
		return 0;
	}
	if(n < vlen){
		if(ulen != nil)
			memmove(ulen, &vlen, sizeof vlen);
		error(Eshort);
	}
	kb = smalloc(vlen > 0 ? vlen : 1);
	if(waserror()){
		free(kb);
		nexterror();
	}
	if(envnativeget(name, kb, vlen, &vlen) < 0)
		error(up->errstr[0] != 0 ? up->errstr : Eio);
	if(vlen > 0)
		memmove(buf, kb, vlen);
	if(ulen != nil)
		memmove(ulen, &vlen, sizeof vlen);
	free(kb);
	poperror();
	free(name);
	poperror();
	return 0;
}

uintptr
sysenvset(sysva_list list)
{
	int nname;
	char *uname, *name, *kb;
	void *uval;
	long n;

	nname = sysva_arg(list, int);
	uname = sysva_arg(list, char*);
	uval = sysva_arg(list, void*);
	n = sysva_arg(list, long);
	if(n < 0 || n > Envvalmax)
		error(Ebadarg);
	if(n > 0 && uval == nil)
		error(Ebadarg);
	name = envnamecopy(uname, nname);
	if(waserror()){
		free(name);
		nexterror();
	}
	if(n > 0 && !okaddr((uintptr)uval, n, 0))
		error(Ebadarg);
	kb = smalloc(n > 0 ? n : 1);
	if(waserror()){
		free(kb);
		nexterror();
	}
	if(n > 0)
		memmove(kb, uval, n);
	envnativeset(name, kb, n);
	free(kb);
	poperror();
	free(name);
	poperror();
	return 0;
}

uintptr
sysenvdelete(sysva_list list)
{
	int nname;
	char *uname, *name;

	nname = sysva_arg(list, int);
	uname = sysva_arg(list, char*);
	name = envnamecopy(uname, nname);
	if(waserror()){
		free(name);
		nexterror();
	}
	envnativedelete(name);
	free(name);
	poperror();
	return 0;
}

uintptr
sysenvlist(sysva_list list)
{
	char *kb;
	void *buf;
	long n, need, r, *uneed;

	buf = sysva_arg(list, void*);
	n = sysva_arg(list, long);
	uneed = sysva_arg(list, long*);
	if(n < 0 || n > Envlistmax)
		error(Ebadarg);
	if(uneed != nil && !okaddr((uintptr)uneed, sizeof(long), 1))
		error(Ebadarg);
	need = 0;
	if(envnativelist(nil, 0, &need) < 0)
		error(up->errstr[0] != 0 ? up->errstr : Eio);
	if(need < 0 || need > Envlistmax)
		error(Etoobig);
	if(buf == nil || n == 0){
		if(uneed != nil)
			memmove(uneed, &need, sizeof need);
		return 0;
	}
	if(!okaddr((uintptr)buf, n, 1))
		error(Ebadarg);
	if(n < need){
		if(uneed != nil)
			memmove(uneed, &need, sizeof need);
		error(Eshort);
	}
	kb = smalloc(need > 0 ? need : 1);
	if(waserror()){
		free(kb);
		nexterror();
	}
	r = envnativelist(kb, need, &need);
	if(r < 0)
		error(up->errstr[0] != 0 ? up->errstr : Eio);
	if(need > 0)
		memmove(buf, kb, need);
	if(uneed != nil)
		memmove(uneed, &need, sizeof need);
	free(kb);
	poperror();
	return 0;
}
