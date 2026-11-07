/*
 * E7.6 native IPv6 Ethernet framing.
 *
 * Chan-free counterpart of ethermediumframe(): it performs the inherited
 * IPv6 neighbor resolution (arpget/arpresolve/ndpsendsol) and builds the
 * Ethernet header.  IPv4/ARP solicitation and the legacy Etherrock channels
 * stay in ethermedium.c and are not linked into the native-only profile.
 */
#include "u.h"
#include "../core/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../core/error.h"

#include "../core/netif.h"
#include "ip.h"
#include "ipv6.h"

typedef struct Etherhdr Etherhdr;
struct Etherhdr
{
	uchar	d[6];
	uchar	s[6];
	uchar	t[2];
};

static int
multicastea(uchar *ea, uchar *ip)
{
	int x;

	switch(x = ipismulticast(ip)){
	case V6:
		ea[0] = 0x33;
		ea[1] = 0x33;
		ea[2] = ip[12];
		ea[3] = ip[13];
		ea[4] = ip[14];
		ea[5] = ip[15];
		break;
	}
	return x;
}

Block*
ethermediumframe6(Fs *f, Ipifc *ifc, Block *bp, uchar *ip, Routehint *rh)
{
	Etherhdr *eh;
	Arpent *a;
	Block *nb;
	uchar mac[6];

	a = arpget(f->arp, bp, V6, ifc, ip, mac, rh);
	if(a != nil){
		nb = nil;
		if(ipforme(f, a->ip) == Rbcast){
			memset(mac, 0xff, ifc->m->maclen);
			nb = arpresolve(f->arp, a, mac, rh);
		} else if(multicastea(mac, a->ip) == V6)
			nb = arpresolve(f->arp, a, mac, rh);
		if(nb == nil){
			if(a->utime - a->ctime < RETRANS_TIMER){
				arprelease(f->arp, a);
				return nil;
			}
			ndpsendsol(f, a);
			return nil;
		}
		bp = nb;
	}

	assert(bp->list == nil);

	bp = padblock(bp, ETHERHDRSIZE);
	if(BLEN(bp) < ETHERMINTU)
		bp = adjustblock(bp, ETHERMINTU);
	eh = (Etherhdr*)bp->rp;
	memmove(eh->s, ifc->mac, sizeof(eh->s));
	memmove(eh->d, mac, sizeof(eh->d));
	eh->t[0] = 0x86;
	eh->t[1] = 0xDD;
	return bp;
}
