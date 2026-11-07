#include	"u.h"
#include	"../core/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../core/error.h"

long
incref(Ref *r)
{
	long old, new;

	do {
		old = r->ref;
		new = old+1;
	} while(!cmpswap(&r->ref, old, new));
	return new;
}

long
decref(Ref *r)
{
	long old, new;

	do {
		old = r->ref;
		if(old <= 0)
			panic("decref pc=%#p", getcallerpc(&r));
		new = old-1;
	} while(!cmpswap(&r->ref, old, new));
	return new;
}

/*
 * Rather than strncpy, which zeros the rest of the buffer, kstrcpy
 * truncates if necessary, always zero terminates, does not zero fill,
 * and puts ... at the end of the string if it's too long.  Usually used to
 * save a string in up->genbuf;
 */
void
kstrcpy(char *s, char *t, int ns)
{
	int nt;

	nt = strlen(t);
	if(nt < ns){
		memmove(s, t, nt);
		s[nt] = '\0';
		return;
	}
	/* too long, truncate */
	nt = ns-1;
	memmove(s, t, nt);
	s[nt] = '\0';
	/* append ... if there is space */
	ns -= 4;
	if(ns < 0)
		return;
	/* look for first byte of UTF-8 sequence by skipping continuation bytes */
	while(ns>0 && (s[--ns]&0xC0)==0x80)
		;
	strcpy(s+ns, "...");
}

/*
 * Atomically replace *p with copy of s
 */
void
kstrdup(char **p, char *s)
{
	int n;
	char *t, *prev;

	n = strlen(s);
	/* if it's a user, we can wait for memory; if not, something's very wrong */
	if(up != nil)
		t = smalloc(n+1);
	else{
		t = malloc(n+1);
		if(t == nil)
			panic("kstrdup: no memory");
	}
	setmalloctag(t, getcallerpc(&p));
	memmove(t, s, n);
	t[n] = '\0';
	prev = *p;
	*p = t;
	free(prev);
}

void
chandevreset(void)
{
	todinit();	/* avoid later reentry causing infinite recursion */
}

char isfrog[256]={
	/*NUL*/	1, 1, 1, 1, 1, 1, 1, 1,
	/*BKS*/	1, 1, 1, 1, 1, 1, 1, 1,
	/*DLE*/	1, 1, 1, 1, 1, 1, 1, 1,
	/*CAN*/	1, 1, 1, 1, 1, 1, 1, 1,
	['/']	1,
	[0x7f]	1,
};

/*
 * Check that the name
 *  a) is in valid memory.
 *  b) is shorter than 2^16 bytes, so it can fit in a 9P string field.
 *  c) contains no frogs.
 * The first byte is known to be addressible by the requester, so the
 * routine works for kernel and user memory both.
 * The parameter slashok flags whether a slash character is an error
 * or a valid character.
 *
 * The parameter dup flags whether the string should be copied
 * out of user space before being scanned the second time.
 * (Otherwise a malicious thread could remove the NUL, causing us
 * to access unchecked addresses.) 
 */
static char*
validname0(char *aname, int slashok, int dup, uintptr pc)
{
	char *ename, *name, *s;
	int c, n;
	Rune r;

	name = aname;
	if((uintptr)name < KZERO){
		if(!dup)
			print("warning: validname called from %#p with user pointer", pc);
		ename = vmemchr(name, 0, (1<<16));
	}else
		ename = memchr(name, 0, (1<<16));

	if(ename==nil || ename-name>=(1<<16))
		error(Etoolong);

	s = nil;
	if(dup){
		n = ename-name;
		s = smalloc(n+1);
		memmove(s, name, n);
		s[n] = 0;
		aname = s;
		name = s;
		setmalloctag(s, pc);
	}
	
	while(*name){
		/* all characters above '~' are ok */
		c = *(uchar*)name;
		if(c >= Runeself)
			name += chartorune(&r, name);
		else{
			if(isfrog[c])
			if(!slashok || c!='/'){
				snprint(up->genbuf, sizeof(up->genbuf), "%s: %q", Ebadchar, aname);
				free(s);
				error(up->genbuf);
			}
			name++;
		}
	}
	return s;
}

void
validname(char *aname, int slashok)
{
	validname0(aname, slashok, 0, getcallerpc(&aname));
}

char*
validnamedup(char *aname, int slashok)
{
	return validname0(aname, slashok, 1, getcallerpc(&aname));
}
