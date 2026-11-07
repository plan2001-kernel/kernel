#include	"u.h"
#include	"../core/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../core/error.h"
#include	"ip.h"
#include	"ipv6.h"

char *v6hdrtypes[Maxhdrtype] =
{
	[HBH]		"HopbyHop",
	[ICMP]		"ICMP",
	[IGMP]		"IGMP",
	[GGP]		"GGP",
	[IPINIP]	"IP",
	[ST]		"ST",
	[TCP]		"TCP",
	[UDP]		"UDP",
	[ISO_TP4]	"ISO_TP4",
	[RH]		"Routinghdr",
	[FH]		"Fraghdr",
	[IDRP]		"IDRP",
	[RSVP]		"RSVP",
	[AH]		"Authhdr",
	[ESP]		"ESP",
	[ICMPv6]	"ICMPv6",
	[NNH]		"Nonexthdr",
	[ISO_IP]	"ISO_IP",
	[IGRP]		"IGRP",
	[OSPF]		"OSPF",
};

/*
 *  well known IPv6 addresses
 */
uchar v6Unspecified[IPaddrlen] = {
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0
};
uchar v6loopback[IPaddrlen] = {
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0x01
};

uchar v6linklocal[IPaddrlen] = {
	0xfe, 0x80, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0
};
uchar v6linklocalmask[IPaddrlen] = {
	0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff,
	0, 0, 0, 0,
	0, 0, 0, 0
};
int v6llpreflen = 8;	/* link-local prefix length in bytes */

uchar v6multicast[IPaddrlen] = {
	0xff, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0
};
uchar v6multicastmask[IPaddrlen] = {
	0xff, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0
};
int v6mcpreflen = 1;	/* multicast prefix length */

uchar v6allnodesN[IPaddrlen] = {
	0xff, 0x01, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0x01
};
uchar v6allroutersN[IPaddrlen] = {
	0xff, 0x01, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0x02
};
uchar v6allnodesNmask[IPaddrlen] = {
	0xff, 0xff, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0
};
int v6aNpreflen = 2;	/* all nodes (N) prefix */

uchar v6allnodesL[IPaddrlen] = {
	0xff, 0x02, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0x01
};
uchar v6allroutersL[IPaddrlen] = {
	0xff, 0x02, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0x02
};
uchar v6allnodesLmask[IPaddrlen] = {
	0xff, 0xff, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0
};
int v6aLpreflen = 2;	/* all nodes (L) prefix */

uchar v6solicitednode[IPaddrlen] = {
	0xff, 0x02, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0x01,
	0xff, 0, 0, 0
};
uchar v6solicitednodemask[IPaddrlen] = {
	0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff,
	0xff, 0x0, 0x0, 0x0
};
int v6snpreflen = 13;

ushort
ptclcsum(Block *bp, int offset, int len)
{
	uchar *addr;
	ulong losum, hisum;
	ushort csum;
	int odd, blocklen, x;

	/* Correct to front of data area */
	while(bp != nil && offset && offset >= BLEN(bp)) {
		offset -= BLEN(bp);
		bp = bp->next;
	}
	if(bp == nil)
		return 0;

	addr = bp->rp + offset;
	blocklen = BLEN(bp) - offset;

	if(bp->next == nil) {
		if(blocklen < len)
			len = blocklen;
		return ptclbsum(addr, len) ^ 0xffff;
	}

	losum = 0;
	hisum = 0;

	odd = 0;
	while(len) {
		x = blocklen;
		if(len < x)
			x = len;

		csum = ptclbsum(addr, x);
		if(odd)
			hisum += csum;
		else
			losum += csum;
		odd = (odd+x) & 1;
		len -= x;

		bp = bp->next;
		if(bp == nil)
			break;
		blocklen = BLEN(bp);
		addr = bp->rp;
	}

	losum += hisum>>8;
	losum += (hisum&0xff)<<8;
	while((csum = losum>>16) != 0)
		losum = csum + (losum & 0xffff);

	return losum ^ 0xffff;
}

enum
{
	Isprefix= 16,
};

#define CLASS(p) ((*(uchar*)(p))>>6)

void
ipv62smcast(uchar *smcast, uchar *a)
{
	ipmove(smcast, v6solicitednode);
	smcast[13] = a[13];
	smcast[14] = a[14];
	smcast[15] = a[15];
}

/*
 *  parse a hex mac address
 */
int
parsemac(uchar *to, char *from, int len)
{
	char nip[4];
	char *p;
	int i;

	p = from;
	memset(to, 0, len);
	for(i = 0; i < len; i++){
		if(p[0] == '\0' || p[1] == '\0')
			break;

		nip[0] = p[0];
		nip[1] = p[1];
		nip[2] = '\0';
		p += 2;

		to[i] = strtoul(nip, 0, 16);
		if(*p == ':')
			p++;
	}
	return i;
}

/*
 *  return multicast version if any
 */
int
ipismulticast(uchar *ip)
{
	if(isv4(ip)){
		if(isv4mcast(&ip[IPv4off]))
			return V4;
	}
	else if(isv6mcast(ip))
		return V6;
	return 0;
}

/*
 *  return ip version of a connection
 */
int
convipvers(Conv *c)
{
	if(isv4(c->hash.raddr) && isv4(c->hash.laddr) || ipcmp(c->hash.raddr, IPnoaddr) == 0)
		return V4;
	else
		return V6;
}

/*
 *  hashing tcp, udp, ... connections
 */
static ulong
iphash(uchar *sa, ushort sp, uchar *da, ushort dp)
{
	uint v;

	v = ((uint)sa[IPaddrlen-1] << 24) ^ ((uint)sp << 16)
	  ^ ((uint)da[IPaddrlen-1] << 8) ^ (uint)dp;
	return v % Nipht;
}

void
iphtadd(Ipht *ht, Iphash *h)
{
	ulong hv;

	if(ipcmp(h->raddr, IPnoaddr) != 0)
		h->match = IPmatchexact;
	else {
		if(ipcmp(h->laddr, IPnoaddr) != 0){
			if(h->lport == 0)
				h->match = IPmatchaddr;
			else
				h->match = IPmatchpa;
		} else {
			if(h->lport == 0)
				h->match = IPmatchany;
			else
				h->match = IPmatchport;
		}
	}
	lock(&ht->lk);
	hv = iphash(h->raddr, h->rport, h->laddr, h->lport);
	h->nextiphash = ht->tab[hv];
	ht->tab[hv] = h;
	unlock(&ht->lk);
}

void
iphtrem(Ipht *ht, Iphash *h)
{
	ulong hv;
	Iphash **l;

	lock(&ht->lk);
	hv = iphash(h->raddr, h->rport, h->laddr, h->lport);
	for(l = &ht->tab[hv]; (*l) != nil; l = &(*l)->nextiphash)
		if(*l == h){
			(*l) = h->nextiphash;
			h->nextiphash = nil;
			break;
		}
	unlock(&ht->lk);
}

/* look for a matching iphash with the following precedence
 *	raddr,rport,laddr,lport
 *	laddr,lport
 *	*,lport
 *	laddr,*
 *	*,*
 */
Iphash*
iphtlook(Ipht *ht, uchar *sa, ushort sp, uchar *da, ushort dp)
{
	ulong hv;
	Iphash *h;

	lock(&ht->lk);
	/* exact 4 pair match (connection) */
	hv = iphash(sa, sp, da, dp);
	for(h = ht->tab[hv]; h != nil; h = h->nextiphash){
		if(h->match != IPmatchexact)
			continue;
		if(sp == h->rport && dp == h->lport
		&& ipcmp(sa, h->raddr) == 0 && ipcmp(da, h->laddr) == 0){
			unlock(&ht->lk);
			return h;
		}
	}

	/* match local address and port */
	hv = iphash(IPnoaddr, 0, da, dp);
	for(h = ht->tab[hv]; h != nil; h = h->nextiphash){
		if(h->match != IPmatchpa)
			continue;
		if(dp == h->lport && ipcmp(da, h->laddr) == 0){
			unlock(&ht->lk);
			return h;
		}
	}

	/* match just port */
	hv = iphash(IPnoaddr, 0, IPnoaddr, dp);
	for(h = ht->tab[hv]; h != nil; h = h->nextiphash){
		if(h->match != IPmatchport)
			continue;
		if(dp == h->lport){
			unlock(&ht->lk);
			return h;
		}
	}

	/* match local address */
	hv = iphash(IPnoaddr, 0, da, 0);
	for(h = ht->tab[hv]; h != nil; h = h->nextiphash){
		if(h->match != IPmatchaddr)
			continue;
		if(ipcmp(da, h->laddr) == 0){
			unlock(&ht->lk);
			return h;
		}
	}

	/* look for something that matches anything */
	hv = iphash(IPnoaddr, 0, IPnoaddr, 0);
	for(h = ht->tab[hv]; h != nil; h = h->nextiphash){
		if(h->match != IPmatchany)
			continue;
		unlock(&ht->lk);
		return h;
	}
	unlock(&ht->lk);
	return nil;
}

/*
 * Checksum adjusting hnputs()
 */
void
hnputs_csum(void *p, ushort v, uchar *pcsum)
{
	ulong csum;
	ushort o;

	csum = nhgets(pcsum)^0xFFFF;
	o = nhgets(p);
	hnputs(p, v);
	if(((uchar*)p - pcsum) & 1){
		o = o << 8 | o >> 8;
		v = v << 8 | v >> 8;
	}
	csum += o ^ 0xFFFF;
	csum += v;
	while(v = csum >> 16)
		csum = (csum & 0xFFFF) + v;
	hnputs(pcsum, csum^0xFFFF);
}
