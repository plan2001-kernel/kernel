#include "u.h"
#include "../core/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../core/error.h"

#include "ip.h"
#include "ipv6.h"

/*
 *  address resolution tables
 */

enum
{
	NHASH		= (1<<6),
	NCACHE		= 256,

	AOK		= 1,
	AWAIT		= 2,

	MAXAGE_TIMER	= 15*60*1000,
};

char *arpstate[] =
{
	"UNUSED",
	"OK",
	"WAIT",
};

/*
 *  one per Fs
 */
struct Arp
{
	RWLock	rwlk;
	Fs	*f;
	Arpent	*hash[NHASH];
	Arpent	cache[NCACHE];
	Arpent	*rxmt[2];
	Proc	*rxmitp;	/* neib sol re-transmit proc */
	Rendez	rxmtq;
	Block 	*dropf, *dropl;
};

char *Ebadarp = "bad arp";

/* quick hash for ip addresses */
#define hashipa(a) (((a)[IPaddrlen-2] + (a)[IPaddrlen-1])%NHASH)

static void 	rxmitproc(void*);

void
arpinit(Fs *f)
{
	f->arp = smalloc(sizeof(Arp));
	f->arp->f = f;
	f->arp->rxmt[0] = nil;
	f->arp->rxmt[1] = nil;
	f->arp->dropf = f->arp->dropl = nil;
	kproc("rxmitproc", rxmitproc, f->arp);
}

static void
freeblistchain(Block *bp)
{
	Block *next;

	while(bp != nil){
		next = bp->list;
		freeblist(bp);
		bp = next;
	}
}

/* take out of re-transmit chain */
static Arpent**
rxmtunchain(Arp *arp, Arpent *a)
{
	Arpent **l;

	for(l = &arp->rxmt[isv4(a->ip) != 0]; *l != nil; l = &((*l)->nextrxt)){
		if(*l == a){
			*l = a->nextrxt;
			break;
		}
	}
	a->nextrxt = nil;
	return l;
}

static void
cleanarpent(Arp *arp, Arpent *a)
{
	Arpent **l;
	Block *bp;

	/* take out of current chain */
	for(l = &arp->hash[hashipa(a->ip)]; *l != nil; l = &((*l)->hash)){
		if(*l == a){
			*l = a->hash;
			break;
		}
	}
	a->hash = nil;

	/* remove from retransmit / timout chain */
	rxmtunchain(arp, a);

	/* queue packets for icmp unreachable for rxmitproc later on, w/o arp lock */
	if((bp = a->hold) != nil){
		if(arp->dropf == nil)
			arp->dropf = bp;
		else
			arp->dropl->list = bp;
		arp->dropl = a->last;
		if(bp == arp->dropf)
			wakeup(&arp->rxmtq);
	}
	a->hold = nil;
	a->last = nil;

	a->ifc = nil;
	a->ifcid = 0;

	a->state = 0;
	a->rxtsrem = 0;

	a->utime = 0;
	a->ctime = 0;
}

/*
 *  create a new arp entry for an ip address on ifc.
 */
static Arpent*
newarpent(Arp *arp, uchar *ip, Ipifc *ifc)
{
	Arpent *a, *e, *f, **l;
	ulong t;

	/* find oldest entry */
	e = &arp->cache[NCACHE];
	a = arp->cache;
	t = a->utime;
	for(f = a+1; t > 0 && f < e; f++){
		if(f->utime < t){
			t = f->utime;
			a = f;
		}
	}
	cleanarpent(arp, a);

	ipmove(a->ip, ip);
	a->ifc = ifc;
	a->ifcid = ifc->ifcid;

	/* insert into new chain */
	l = &arp->hash[hashipa(ip)];
	a->hash = *l;
	*l = a;

	return a;
}

static Arpent*
arplookup(Arp *arp, Ipifc *ifc, uchar *ip)
{
	Arpent *a;

	for(a = arp->hash[hashipa(ip)]; a != nil; a = a->hash){
		if(a->ifc == ifc && a->ifcid == ifc->ifcid && ipcmp(ip, a->ip) == 0)
			return a;
	}
	return nil;
}

static int
arphit(Arpent *a, uchar *mac, int maclen, Routehint *rh)
{
	if(a->state == AOK){
		memmove(mac, a->mac, maclen);
		a->utime = NOW;
		if(a->utime - a->ctime < MAXAGE_TIMER){
			if(rh != nil)
				rh->a = a;
			return 1;
		}
	}
	return 0;
}

/*
 *  fill in the media address if we have it.  Otherwise return an
 *  Arpent that represents the state of the address resolution FSM
 *  for ip.  Add the packet to be sent onto the list of packets
 *  waiting for ip->mac to be resolved.
 */
Arpent*
arpget(Arp *arp, Block *bp, int version, Ipifc *ifc, uchar *ip, uchar *mac, Routehint *rh)
{
	uchar v6ip[IPaddrlen];
	Arpent *a;

	if(version == V4){
		v4tov6(v6ip, ip);
		ip = v6ip;
	}

	if(rh != nil
	&& (a = rh->a) != nil
	&& a->ifc == ifc
	&& a->ifcid == ifc->ifcid
	&& ipcmp(ip, a->ip) == 0
	&& arphit(a, mac, ifc->m->maclen, nil))
		return nil;

	rlock(&arp->rwlk);
	if((a = arplookup(arp, ifc, ip)) != nil
	&& arphit(a, mac, ifc->m->maclen, rh)){
		runlock(&arp->rwlk);
		return nil;
	}
	if(rh != nil)
		rh->a = nil;
	runlock(&arp->rwlk);

	wlock(&arp->rwlk);
	if((a = arplookup(arp, ifc, ip)) == nil)
		a = newarpent(arp, ip, ifc);
	else if(arphit(a, mac, ifc->m->maclen, rh)){
		wunlock(&arp->rwlk);
		return nil;
	}
	a->state = AWAIT;
	a->utime = NOW;
	if(bp != nil){
		/* needs to be a single block */
		assert(bp->list == nil);

		if(a->hold == nil)
			a->hold = bp;
		else
			a->last->list = bp;
		a->last = bp;
	}
	return a;		/* return with arp locked */
}

/*
 * continue address resolution for the entry,
 * schedule it on the retransmit / timeout chains
 * and unlock the arp cache.
 */
void
arpcontinue(Arp *arp, Arpent *a)
{
	Arpent **l;
	Block *bp;

	/* remove all but the last message */
	while((bp = a->hold) != nil){
		if(bp == a->last)
			break;
		a->hold = bp->list;
		freeblist(bp);
	}

	/* try to keep it around for a second more */
	a->ctime = a->utime = NOW;

	/* put on end of re-transmit / timeout chain */
	for(l = rxmtunchain(arp, a); *l != nil; l = &(*l)->nextrxt)
		;
	*l = a;

	if(l == &arp->rxmt[0] || l == &arp->rxmt[1])
		wakeup(&arp->rxmtq);

	wunlock(&arp->rwlk);
}

/*
 * called with arp locked
 */
void
arprelease(Arp *arp, Arpent*)
{
	wunlock(&arp->rwlk);
}


/*
 * Copy out the mac address from the Arpent.  Return the
 * first block waiting to get sent to this mac address.
 *
 * called with arp locked
 */
Block*
arpresolve(Arp *arp, Arpent *a, uchar *mac, Routehint *rh)
{
	Block *bp;

	memmove(a->mac, mac, a->ifc->m->maclen);
	if(a->state == AWAIT) {
		rxmtunchain(arp, a);
		a->rxtsrem = 0;
	}
	bp = a->hold;
	a->hold = a->last = nil;
	a->ctime = a->utime = NOW;
	a->state = AOK;
	if(rh != nil)
		rh->a = a;
	wunlock(&arp->rwlk);

	if(bp != nil){
		freeblistchain(bp->list);
		bp->list = nil;
	}
	return bp;
}

int
arpenter(Fs *fs, int version, uchar *ip, uchar *mac, int maclen, uchar *ia, Ipifc *ifc, int refresh)
{
	Routehint rh;
	Block *bp, *next;
	Arpent *a;
	Route *r;
	Arp *arp;

	if(ifc->m == nil || maclen < ifc->m->maclen || ifc->m->maclen == 0)
		return -1;

	rh.r = nil;
	rh.a = nil;
	switch(version){
	case V4:
		return -1;	/* IPv4 ARP disabled in the native-only profile */
	case V6:
		r = v6lookup(fs, ip, ia, &rh);
		break;
	default:
		panic("arpenter: version %d", version);
	}

	if(r == nil || r->tree.ifc != ifc || (r->tree.type & (Rbcast|Rmulti)) != 0)
		return -1;

	arp = fs->arp;

	wlock(&arp->rwlk);
	if((a = arplookup(arp, ifc, ip)) == nil){
		if(refresh){
			wunlock(&arp->rwlk);
			return 0;
		}
		a = newarpent(arp, ip, ifc);
	}
	bp = a->hold;
	a->hold = a->last = nil;
	arpresolve(arp, a, mac, &rh);	/* unlocks arp */
	for(; bp != nil; bp = next){
		next = bp->list;
		bp->list = nil;
		if(waserror()){
			freeblistchain(next);
			break;
		}
		ipifcoput(ifc, bp, version, ip, &rh);
		poperror();
	}
	return 1;
}

/*
 * arpforme() checks if we should respond to arp/ndp on a specific interface.
 * 
 * returns Runi if targ is a non-tentative local address on ifc.
 * returns Rproxy if we have a proxy route for targ to another interface.
 */
int
arpforme(Fs *fs, int version, uchar *targ, uchar *src, Ipifc *ifc)
{
	Iplifc *lifc;
	Route *r;

	if(version == V4)
		return 0;
	lifc = iplocalonifc(ifc, targ);
	if(lifc != nil){
		if(lifc->tentative)
			return 0;
		return Runi;
	}
	r = v6lookup(fs, targ, src, nil);
	if(r == nil || r->tree.ifc == ifc && r->tree.ifcid == ifc->ifcid)
		return 0;
	return r->tree.type & Rproxy;
}

void
ndpsendsol(Fs *f, Arpent *a)
{
	Ipifc *ifc = a->ifc;
	uchar targ[IPaddrlen], src[IPaddrlen];

	if(a->rxtsrem == 0)
		a->rxtsrem = MAX_MULTICAST_SOLICIT;
	else
		a->rxtsrem--;

	/* try to use source address of original packet */
	ipmove(targ, a->ip);
	if(a->last != nil){
		ipmove(src, ((Ip6hdr*)a->last->rp)->ip6hdr.src);
		arpcontinue(f->arp, a);
		if(arpforme(f, V6, src, targ, ifc))
			goto send;
	} else {
		arpcontinue(f->arp, a);
	}
	if(!ipv6local(ifc, src, 0, targ))
		return;
send:
	if(!waserror()){
		icmpns6(f, src, SRC_UNI, targ, TARG_MULTI, ifc->mac, ifc->m->maclen);
		poperror();
	}
}

static Block*
rxmt(Arp *arp)
{
	Arpent *a;
	Block *bp;
	Ipifc *ifc;

	wlock(&arp->rwlk);

	/* retransmit ipv6 solicitations */
	while((a = arp->rxmt[0]) != nil && NOW - a->ctime > 3*RETRANS_TIMER/4){
		if(a->rxtsrem > 0 && (ifc = a->ifc) != nil && canrlock(&ifc->rwlk)){
			if(a->ifcid == ifc->ifcid){
				ndpsendsol(arp->f, a);	/* unlocks arp */
				runlock(&ifc->rwlk);

				wlock(&arp->rwlk);
				continue;
			}
			runlock(&ifc->rwlk);
		}
		cleanarpent(arp, a);
	}

	bp = arp->dropf;
	arp->dropf = arp->dropl = nil;

	wunlock(&arp->rwlk);

	return bp;
}

static void
drop(Fs *f, Block *bp)
{
	Routehint rh;
	Route *r;
	Ipifc *ifc;
	Block *next;

	for(; bp != nil; bp = next){
		next = bp->list;
		bp->list = nil;

		rh.r = nil;
		rh.a = nil;
		if((bp->rp[0]&0xF0) == IP_VER4){
			freeblist(bp);
			continue;
		}
		r = v6lookup(f, ((Ip6hdr*)bp->rp)->ip6hdr.src, ((Ip6hdr*)bp->rp)->ip6hdr.dst, &rh);
		if(r != nil && (ifc = r->tree.ifc) != nil && canrlock(&ifc->rwlk)){
			if(ifc->ifcid == r->tree.ifcid && !waserror()){
				icmpnohost6(f, ifc, bp, &rh);
				poperror();
			}
			runlock(&ifc->rwlk);
		}
		freeblist(bp);
	}
}

static int
rxready(void *v)
{
	Arp *arp = (Arp *)v;

	return arp->rxmt[0] != nil || arp->rxmt[1] != nil || arp->dropf != nil;
}

static void
rxmitproc(void *v)
{
	Arp *arp = v;

	arp->rxmitp = up;
	if(waserror()){
		arp->rxmitp = nil;
		pexit("hangup");
	}
	for(;;){
		sleep(&arp->rxmtq, rxready, v);
		drop(arp->f, rxmt(arp));
		tsleep(&arp->rxmtq, return0, nil, RETRANS_TIMER/4);
	}
}
