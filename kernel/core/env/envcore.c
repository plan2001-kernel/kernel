#include	"u.h"
#include	"../lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../error.h"
#include	"envapi.h"

enum
{
	Maxenvsize = 1*MB,
	Maxvalsize = Maxenvsize/2,

	DELTAENV = 32,
};

static Egrp	*envnativegrp(void);
static void	envvalidname(char *name);
static void	envsetvalbytes(Egrp *eg, char *name, void *val, long len);

static Egrp	confegrp;	/* global environment group containing the kernel configuration */

static Evalue**
envhash(Egrp *eg, char *name)
{
	uint c, h = 0;
	while(c = *name++)
		h = h*131 + c;
	return &eg->hash[h % ENVHASH];
}

static Evalue*
lookupname(Evalue *e, char *name)
{
	while(e != nil){
		if(strcmp(e->name, name) == 0)
			break;
		e = e->hash;
	}
	return e;
}

static void*
envrealloc(Egrp *eg, void *old, int newsize)
{
	int oldsize = old != nil ? msize(old) : 0;
	void *new;

	if(newsize == 0){
		eg->alloc -= oldsize;
		free(old);
		return nil;
	}
	if(newsize < 0 || eg != &confegrp && (eg->alloc + newsize) - oldsize > Maxenvsize)
		error(Enomem);
	new = realloc(old, newsize);
	if(new == nil)
		error(Enomem);
	eg->alloc += msize(new) - oldsize;
	setmalloctag(new, getcallerpc(&eg));
	return new;
}

Egrp*
newegrp(void)
{
	Egrp *eg;

	eg = malloc(sizeof(Egrp));
	if(eg == nil)
		error(Enomem);
	eg->ref.ref = 1;
	return eg;
}

void
envcpy(Egrp *to, Egrp *from)
{
	Evalue *e, *ne, **h;
	int i, n;

	rlock(&from->rwlk);
	if(waserror()){
		runlock(&from->rwlk);
		nexterror();
	}
	to->nent = 0;
	to->ent = envrealloc(to, nil, ROUND(from->nent, DELTAENV) * sizeof(Evalue*));
	for(i = 0; i < from->nent; i++){
		e = from->ent[i];
		if(e == nil)
			continue;
		h = envhash(to, e->name);
		n = strlen(e->name)+1;
		ne = envrealloc(to, nil, sizeof(Evalue)+n);
		memmove(ne->name, e->name, n);
		ne->value = nil;
		ne->len = 0;
		ne->hash = *h, *h = ne;
		to->ent[to->nent++] = ne;
		if(e->len > 0){
			ne->value = envrealloc(to, ne->value, e->len);
			memmove(ne->value, e->value, e->len);
			ne->len = e->len;
		}
	}
	to->low = to->nent;
	runlock(&from->rwlk);
	poperror();
}

void
closeegrp(Egrp *eg)
{
	Evalue *e;
	int i;

	if(decref(&eg->ref) || eg == &confegrp)
		return;
	for(i = 0; i < eg->nent; i++){
		e = eg->ent[i];
		if(e == nil)
			continue;
		free(e->value);
		free(e);
	}
	free(eg->ent);
	free(eg);
}

static void
envsetvalbytes(Egrp *eg, char *name, void *val, long len)
{
	Evalue *e, **h;
	Evalue *volatile ne;
	char *volatile nv;
	int i, n;

	if(len < 0 || len > Maxvalsize)
		error(Etoobig);
	n = strlen(name)+1;

	wlock(&eg->rwlk);
	ne = nil;
	nv = nil;
	if(waserror()){
		if(nv != nil)
			envrealloc(eg, nv, 0);
		if(ne != nil)
			envrealloc(eg, ne, 0);
		wunlock(&eg->rwlk);
		nexterror();
	}

	h = envhash(eg, name);
	e = lookupname(*h, name);
	if(e != nil){
		if(len > 0){
			e->value = envrealloc(eg, e->value, (int)len);
			memmove(e->value, val, len);
		}else
			e->value = envrealloc(eg, e->value, 0);
		e->len = (int)len;
		wunlock(&eg->rwlk);
		poperror();
		return;
	}

	for(i = eg->low; i < eg->nent; i++)
		if(eg->ent[i] == nil)
			break;

	if(i >= eg->nent){
		if((eg->nent % DELTAENV) == 0)
			eg->ent = envrealloc(eg, eg->ent, (eg->nent+DELTAENV) * sizeof(Evalue*));
		i = eg->nent;
	}

	ne = envrealloc(eg, nil, sizeof(Evalue)+n);
	memmove(ne->name, name, n);
	ne->value = nil;
	ne->len = (int)len;
	ne->hash = nil;
	if(len > 0){
		nv = envrealloc(eg, nil, (int)len);
		memmove(nv, val, len);
	}
	ne->value = nv;
	nv = nil;
	ne->hash = *h, *h = ne;
	eg->ent[i] = ne;
	if(i >= eg->nent)
		eg->nent = i+1;
	eg->low = i+1;
	ne = nil;

	wunlock(&eg->rwlk);
	poperror();
}

void
envsetval(Egrp *eg, char *name, char *val)
{
	long len;

	len = strlen(val);
	if(len > Maxvalsize)
		error(Etoobig);
	envsetvalbytes(eg, name, val, len);
}

/*
 *  to let the kernel set environment variables
 */
void
ksetenv(char *ename, char *eval, int conf)
{
	Egrp *eg;
	int n;

	if(up == nil || up->pgrp == nil)
		error(Eperm);
	if(ename == nil || eval == nil)
		error(Ebadarg);
	n = strlen(ename);
	if(n + (conf ? 4 : 3) >= 2*KNAMELEN || n+1 > sizeof(up->genbuf))
		error(Etoolong);
	validname(ename, 0);
	if(n == 0 || strcmp(ename, ".") == 0 || strcmp(ename, "..") == 0)
		error(Ebadarg);

	if(conf){
		if(up->egrp != &confegrp && !iseve())
			error(Eperm);
		eg = &confegrp;
	}else{
		if(up->egrp == nil)
			error(Eperm);
		eg = up->egrp;
	}
	envsetval(eg, ename, eval);
}

static Egrp*
envnativegrp(void)
{
	if(up == nil || up->egrp == nil)
		error(Eperm);
	return up->egrp;
}

static void
envvalidname(char *name)
{
	char *nul;
	int n;

	if(name == nil)
		error(Ebadarg);
	nul = memchr(name, 0, sizeof(up->genbuf));
	if(nul == nil)
		error(Etoolong);
	n = nul - name;
	validname(name, 0);
	if(n == 0 || (n == 1 && name[0] == '.') ||
	   (n == 2 && name[0] == '.' && name[1] == '.'))
		error(Ebadarg);
}

int
envnativeget(char *name, void *buf, long n, long *len)
{
	Egrp *eg;
	Evalue *e;

	eg = envnativegrp();
	envvalidname(name);
	if(len == nil)
		error(Ebadarg);

	rlock(&eg->rwlk);
	if(waserror()){
		runlock(&eg->rwlk);
		nexterror();
	}
	e = lookupname(*envhash(eg, name), name);
	if(e == nil)
		error(Enonexist);
	*len = e->len;
	if(buf != nil){
		if(n < e->len)
			error(Eshort);
		if(e->len > 0)
			memmove(buf, e->value, e->len);
	}
	runlock(&eg->rwlk);
	poperror();
	return 0;
}

int
envnativeset(char *name, void *val, long len)
{
	Egrp *eg;

	eg = envnativegrp();
	envvalidname(name);
	if(len < 0 || len > Maxvalsize)
		error(Etoobig);
	if(val == nil && len != 0)
		error(Ebadarg);
	envsetvalbytes(eg, name, val, len);
	return 0;
}

int
envnativedelete(char *name)
{
	Egrp *eg;
	Evalue *e, **h;
	int i;

	eg = envnativegrp();
	envvalidname(name);

	wlock(&eg->rwlk);
	if(waserror()){
		wunlock(&eg->rwlk);
		nexterror();
	}
	e = lookupname(*envhash(eg, name), name);
	if(e == nil)
		error(Enonexist);
	for(i = 0; i < eg->nent; i++)
		if(eg->ent[i] == e)
			break;
	if(i == eg->nent)
		error(Egreg);
	for(h = envhash(eg, e->name); *h != nil; h = &(*h)->hash){
		if(*h == e){
			*h = e->hash;
			break;
		}
	}
	eg->ent[i] = nil;
	if(i < eg->low)
		eg->low = i;

	envrealloc(eg, e->value, 0);
	envrealloc(eg, e, 0);

	wunlock(&eg->rwlk);
	poperror();
	return 0;
}

#ifdef PLAN2001_LIFECYCLE_DIAG
/*
 * Test-only deterministic fault trigger for the lifecycle diagnostic kernel.
 * If the current process environment carries NAME with exactly WANT, consume
 * the entry and return 1.  This never raises and is compiled out of the
 * canonical kernel, so production process-creation paths are unchanged.
 */
int
envfaulttake(char *name, char *want)
{
	Egrp *eg;
	Evalue *e, **h;
	int i, match, wlen;

	if(up == nil || up->egrp == nil || name == nil || want == nil)
		return 0;
	eg = up->egrp;
	match = 0;
	wlen = strlen(want);
	wlock(&eg->rwlk);
	e = lookupname(*envhash(eg, name), name);
	if(e != nil && e->len == wlen &&
	   (wlen == 0 || memcmp(e->value, want, wlen) == 0)){
		match = 1;
		for(i = 0; i < eg->nent; i++)
			if(eg->ent[i] == e)
				break;
		if(i < eg->nent){
			for(h = envhash(eg, e->name); *h != nil; h = &(*h)->hash)
				if(*h == e){
					*h = e->hash;
					break;
				}
			eg->ent[i] = nil;
			if(i < eg->low)
				eg->low = i;
			envrealloc(eg, e->value, 0);
			envrealloc(eg, e, 0);
		}
	}
	wunlock(&eg->rwlk);
	return match;
}
#endif

static void
envputbe32(uchar *p, ulong v)
{
	p[0] = v >> 24;
	p[1] = v >> 16;
	p[2] = v >> 8;
	p[3] = v;
}

long
envnativelist(void *buf, long n, long *need)
{
	Egrp *eg;
	Evalue *e;
	uchar *p;
	long add, total;
	int i, nlen;

	eg = envnativegrp();

	rlock(&eg->rwlk);
	if(waserror()){
		runlock(&eg->rwlk);
		nexterror();
	}
	total = 0;
	for(i = 0; i < eg->nent; i++){
		e = eg->ent[i];
		if(e == nil)
			continue;
		nlen = strlen(e->name);
		if(nlen < 0 || e->len < 0)
			error(Etoobig);
		add = 8 + (long)nlen + (long)e->len;
		if(add < 0 || total > 0x7fffffffL - add)
			error(Etoobig);
		total += add;
	}
	if(need != nil)
		*need = total;
	if(buf == nil || n == 0){
		runlock(&eg->rwlk);
		poperror();
		return 0;
	}
	if(n < total)
		error(Eshort);
	p = buf;
	for(i = 0; i < eg->nent; i++){
		e = eg->ent[i];
		if(e == nil)
			continue;
		nlen = strlen(e->name);
		envputbe32(p, nlen);
		p += 4;
		envputbe32(p, e->len);
		p += 4;
		memmove(p, e->name, nlen);
		p += nlen;
		if(e->len > 0){
			memmove(p, e->value, e->len);
			p += e->len;
		}
	}
	runlock(&eg->rwlk);
	poperror();
	return total;
}

/*
 * Return a copy of configuration environment as a sequence of strings.
 * The strings alternate between name and value.  A zero length name string
 * indicates the end of the list
 */
char *
getconfenv(void)
{
	Egrp *eg = &confegrp;
	Evalue *e;
	char *p, *q;
	int i, n, m;

	rlock(&eg->rwlk);
	n = 1;
	for(i = 0; i < eg->nent; i++){
		e = eg->ent[i];
		if(e == nil)
			continue;
		n += strlen(e->name)+e->len+2;
	}
	p = malloc(n);
	if(p == nil){
		runlock(&eg->rwlk);
		error(Enomem);
	}
	q = p;
	for(i = 0; i < eg->nent; i++){
		e = eg->ent[i];
		if(e == nil)
			continue;
		n = strlen(e->name)+1;
		memmove(q, e->name, n);
		q += n;
		memmove(q, e->value, e->len);
		for(m = e->len; m > 0; m--){
			if(q[m-1] != '\0')
				break;
		}
		for(n = 0; n < m; n++){
			if(q[n] == '\0')
				q[n] = ' ';
		}
		q[m] = 0;
		q += m+1;
	}
	*q = '\0';
	runlock(&eg->rwlk);

	return p;
}
