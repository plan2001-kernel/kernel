#include	"u.h"
#include	"../core/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../core/error.h"

#include	<authsrv.h>

char	*eve;
char	hostdomain[DOMLEN];

/*
 *  return true if current user is eve
 */
int
iseve(void)
{
	return strcmp(eve, up->user) == 0;
}

/*
 * fversion is the version/msize negotiation for a 9P mount channel.
 * Canonical Plan2001-O has no mnt Dev and no pathname mount transport,
 * so no channel can be a 9P mount.  Fail before looking at the fd or
 * touching user memory; no Chan reference is taken or needs unwinding.
 */
uintptr
sysfversion(sysva_list list)
{
	USED(list);
	error("fversion: 9P mount transport not supported");
}

uintptr
sys_fsession(sysva_list list)
{
	int fd;
	char *str;
	uint len;

	/* deprecated; backwards compatibility only */
	fd = sysva_arg(list, int);
	str = sysva_arg(list, char*);
	len = sysva_arg(list, uint);
	if(len == 0)
		error(Ebadarg);
	validaddr((uintptr)str, len, 1);
	*str = '\0';
	USED(fd);
	return 0;
}

/*
 * fauth authenticates on a 9P mount channel.  With no mount transport
 * there is no channel to authenticate, so fail explicitly without taking
 * a Chan reference or allocating/copying the aname.
 */
uintptr
sysfauth(sysva_list list)
{
	USED(list);
	error("fauth: 9P mount transport not supported");
}

/*
 *  called by devcons() for user device
 *
 *  anyone can become none
 */
long
userwrite(char *a, int n)
{
	if(n!=4 || strncmp(a, "none", 4)!=0)
		error(Eperm);
	procsetuser("none");
	return n;
}

/*
 *  called by devcons() for host domain
 */
long
hostdomainwrite(char *a, int n)
{
	char buf[DOMLEN];

	if(!iseve())
		error(Eperm);
	if(n <= 0 || n >= DOMLEN)
		error(Ebadarg);
	memset(buf, 0, DOMLEN);
	strncpy(buf, a, n);
	if(buf[0] == 0)
		error(Ebadarg);
	memmove(hostdomain, buf, DOMLEN);
	return n;
}
