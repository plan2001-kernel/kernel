#include	"u.h"
#include	"../core/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../core/error.h"

#include	"ip.h"

static char *statnames[] =
{
[Forwarding]	"Forwarding",
[DefaultTTL]	"DefaultTTL",
[InReceives]	"InReceives",
[InHdrErrors]	"InHdrErrors",
[InAddrErrors]	"InAddrErrors",
[ForwDatagrams]	"ForwDatagrams",
[InUnknownProtos]	"InUnknownProtos",
[InDiscards]	"InDiscards",
[InDelivers]	"InDelivers",
[OutRequests]	"OutRequests",
[OutDiscards]	"OutDiscards",
[OutNoRoutes]	"OutNoRoutes",
[ReasmTimeout]	"ReasmTimeout",
[ReasmReqds]	"ReasmReqds",
[ReasmOKs]	"ReasmOKs",
[ReasmFails]	"ReasmFails",
[FragOKs]	"FragOKs",
[FragFails]	"FragFails",
[FragCreates]	"FragCreates",
};


static void
initfrag(IP *ip, int size)
{
	Fragment6 *fq6, *eq6;


	ip->fragfree6 = (Fragment6*)malloc(sizeof(Fragment6) * size);
	if(ip->fragfree6 == nil)
		panic("initfrag");

	eq6 = &ip->fragfree6[size];
	for(fq6 = ip->fragfree6; fq6 < eq6; fq6++)
		fq6->next = fq6+1;

	ip->fragfree6[size-1].next = nil;
}

void
ip_init(Fs *f)
{
	IP *ip;

	ip = smalloc(sizeof(IP));
	ip->stats[DefaultTTL] = MAXTTL;
	initfrag(ip, 100);
	f->ip = ip;

	ip_init_6(f);
}

void
iprouting(Fs *f, int on)
{
	f->ip->iprouting = on;
	if(f->ip->iprouting==0)
		f->ip->stats[Forwarding] = 2;
	else
		f->ip->stats[Forwarding] = 1;
}



int
ipstats(Fs *f, char *buf, int len)
{
	IP *ip;
	char *p, *e;
	int i;

	ip = f->ip;
	p = buf;
	e = p+len;
	for(i = 0; i < Nipstats; i++)
		p = seprint(p, e, "%s: %llud\n", statnames[i], ip->stats[i]);
	return p - buf;
}
