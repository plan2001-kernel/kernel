/*
 * E7.7 IPv6-only profile helpers.
 *
 * The native-only target removes tcp.c, so it must provide the inherited
 * tcpmssclamp symbol used by ipoput6's gated-forwarding path.  There is no
 * native TCP6 stream API in this profile; the no-op is documented in
 * docs/STATUS.md.
 */
#include	"u.h"
#include	"../core/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../core/error.h"

#include	"ip.h"
#include	"ipv6.h"

void
tcpmssclamp(uchar *p, int n, int mtu)
{
	USED(p);
	USED(n);
	USED(mtu);
}

char*
icmpconnect(Conv *c, char **argv, int argc)
{
	char *e;

	e = Fsstdconnect(c, argv, argc);
	if(e != nil)
		return e;
	Fsconnected(c, e);

	return nil;
}

int
icmpstate(Conv *c, char *state, int n)
{
	USED(c);
	return snprint(state, n, "%s qin %d qout %d\n",
		"Datagram",
		c->rq ? qlen(c->rq) : 0,
		c->wq ? qlen(c->wq) : 0
	);
}

char*
icmpannounce(Conv *c, char **argv, int argc)
{
	char *e;

	e = Fsstdannounce(c, argv, argc);
	if(e != nil)
		return e;
	Fsconnected(c, nil);

	return nil;
}
