/*
 * E7.7 native-only route table.
 *
 * Generated from iproute.c with the E7_IPV6_ONLY branches selected: V4
 * lookup/source and the legacy #I route-file parser are absent.  Legacy
 * profiles still link iproute.c.
 */
#include	"u.h"
#include	"../core/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../core/error.h"

#include	"ip.h"

static void	walkadd(Fs*, Route**, Route*);
static void	addnode(Fs*, Route**, Route*);
static void	calcd(Route*);

/* these are used for all instances of IP */
static Route*	v4freelist;
static Route*	v6freelist;
static RWLock	routelock;
static ulong	v4routegeneration, v6routegeneration;

static void
freeroute(Route *r)
{
	Route **l;

	r->tree.ref = 0;
	r->tree.left = nil;
	r->tree.right = nil;
	if(r->tree.type & Rv4)
		l = &v4freelist;
	else
		l = &v6freelist;
	r->tree.mid = *l;
	*l = r;
}

static Route*
allocroute(int type)
{
	Route *r, **l;
	int n;

	if(type & Rv4){
		n = sizeof(RouteTree) + sizeof(V4route);
		l = &v4freelist;
	} else {
		n = sizeof(RouteTree) + sizeof(V6route);
		l = &v6freelist;
	}

	if((r = *l) != nil)
		*l = r->tree.mid;
	else
		r = smalloc(n);
	memset(r, 0, n);
	r->tree.type = type;
	r->tree.ifc = nil;
	r->tree.ref = 1;

	return r;
}

static void
addqueue(Route **q, Route *r)
{
	Route *l;

	if(r == nil)
		return;

	l = allocroute(r->tree.type);
	l->tree.left = r;
	l->tree.mid = *q;
	*q = l;
}

/*
 *   compare 2 v6 addresses
 */
static int
lcmp(ulong *a, ulong *b)
{
	int i;

	for(i = 0; i < IPllen; i++){
		if(a[i] > b[i])
			return 1;
		if(a[i] < b[i])
			return -1;
	}
	return 0;
}

/*
 *  compare 2 v4 or v6 ranges
 */
enum
{
	Rpreceeds,	/* a left of b */
	Rfollows,	/* a right of b */
	Requals,	/* a equals b */
	Rcontains,	/* a contians b */
	Roverlaps,	/* a overlaps b */
};

static int
rangecompare(Route *a, Route *b)
{
	if(a->tree.type & Rv4){
		if(a->v4.endaddress < b->v4.address)
			return Rpreceeds;
		if(a->v4.address > b->v4.endaddress)
			return Rfollows;
		if(a->v4.address <= b->v4.address
		&& a->v4.endaddress >= b->v4.endaddress){
			if(a->v4.address == b->v4.address
			&& a->v4.endaddress == b->v4.endaddress){
				if(a->v4.source <= b->v4.source
				&& a->v4.endsource >= b->v4.endsource){
					if(a->v4.source == b->v4.source
					&& a->v4.endsource == b->v4.endsource)
						return Requals;
					return Rcontains;
				}
				return Roverlaps;
			}
			return Rcontains;
		}
		return Roverlaps;
	}

	if(lcmp(a->v6.endaddress, b->v6.address) < 0)
		return Rpreceeds;
	if(lcmp(a->v6.address, b->v6.endaddress) > 0)
		return Rfollows;
	if(lcmp(a->v6.address, b->v6.address) <= 0
	&& lcmp(a->v6.endaddress, b->v6.endaddress) >= 0){
		if(lcmp(a->v6.address, b->v6.address) == 0
		&& lcmp(a->v6.endaddress, b->v6.endaddress) == 0){
			if(lcmp(a->v6.source, b->v6.source) <= 0
			&& lcmp(a->v6.endsource, b->v6.endsource) >= 0){
				if(lcmp(a->v6.source, b->v6.source) == 0
				&& lcmp(a->v6.endsource, b->v6.endsource) == 0)
					return Requals;
				return Rcontains;
			}
			return Roverlaps;
		}
		return Rcontains;
	}
	return Roverlaps;
}

/* return 1 if a matches b, otherwise 0 */
static int
matchroute(Route *a, Route *b)
{
	if(a == b)
		return 1;

	if((a->tree.type^b->tree.type) & (Rifc|Runi|Rmulti|Rbcast))
		return 0;

	if(a->tree.type & Rv4){
		if(memcmp(a->v4.gate, IPnoaddr+IPv4off, IPv4addrlen) != 0
		&& memcmp(a->v4.gate, b->v4.gate, IPv4addrlen) != 0)
			return 0;
	} else {
		if(ipcmp(a->v6.gate, IPnoaddr) != 0
		&& ipcmp(a->v6.gate, b->v6.gate) != 0)
			return 0;
	}

	if(a->tree.ifc != b->tree.ifc)
		return 0;

	if(*a->tree.tag != 0 && strncmp(a->tree.tag, b->tree.tag, sizeof(a->tree.tag)) != 0)
		return 0;

	return 1;
}

static void
copygate(Route *old, Route *new)
{
	old->tree.type = new->tree.type;
	old->tree.ifc = new->tree.ifc;
	old->tree.ifcid = new->tree.ifcid;
	if(new->tree.type & Rv4)
		memmove(old->v4.gate, new->v4.gate, IPv4addrlen);
	else
		ipmove(old->v6.gate, new->v6.gate);
	strncpy(old->tree.tag, new->tree.tag, sizeof(new->tree.tag));
}

/*
 *  walk down a tree adding nodes back in
 */
static void
walkadd(Fs *f, Route **root, Route *p)
{
	Route *l, *r;

	l = p->tree.left;
	r = p->tree.right;
	p->tree.left = nil;
	p->tree.right = nil;
	addnode(f, root, p);
	if(l != nil)
		walkadd(f, root, l);
	if(r != nil)
		walkadd(f, root, r);
}

/*
 *  calculate depth
 */
static void
calcd(Route *p)
{
	Route *q;
	int d;

	if(p != nil) {
		d = 0;
		q = p->tree.left;
		if(q != nil)
			d = q->tree.depth;
		q = p->tree.right;
		if(q != nil && q->tree.depth > d)
			d = q->tree.depth;
		q = p->tree.mid;
		if(q != nil && q->tree.depth > d)
			d = q->tree.depth;
		p->tree.depth = d+1;
	}
}

/*
 *  balance the tree at the current node
 */
static void
balancetree(Route **cur)
{
	Route *p, *l, *r;
	int dl, dr;

	/*
	 * if left and right are
	 * too out of balance,
	 * rotate tree node
	 */
	p = *cur;
	dl = 0; if((l = p->tree.left) != nil) dl = l->tree.depth;
	dr = 0; if((r = p->tree.right) != nil) dr = r->tree.depth;

	if(dl > dr+1) {
		p->tree.left = l->tree.right;
		l->tree.right = p;
		*cur = l;
		calcd(p);
		calcd(l);
	} else
	if(dr > dl+1) {
		p->tree.right = r->tree.left;
		r->tree.left = p;
		*cur = r;
		calcd(p);
		calcd(r);
	} else
		calcd(p);
}

/*
 *  add a new node to the tree
 */
static void
addnode(Fs *f, Route **cur, Route *new)
{
	Route *p;

	p = *cur;
	if(p == nil) {
		*cur = new;
		new->tree.depth = 1;
		return;
	}

	switch(rangecompare(new, p)){
	case Rpreceeds:
		addnode(f, &p->tree.left, new);
		break;
	case Rfollows:
		addnode(f, &p->tree.right, new);
		break;
	case Rcontains:
		/*
		 *  if new node is superset
		 *  of tree node,
		 *  replace tree node and
		 *  queue tree node to be
		 *  merged into root.
		 */
		*cur = new;
		new->tree.depth = 1;
		addqueue(&f->queue, p);
		break;
	case Requals:
		/*
		 *  supercede the old entry if the old one isn't
		 *  a local interface.
		 */
		if((p->tree.type & Rifc) == 0)
			copygate(p, new);
		else if(new->tree.type & Rifc){
			p->tree.type = (p->tree.type & ~(Rproxy|Rtrans)) | (new->tree.type & (Rproxy|Rtrans));
			p->tree.ref++;
		}
		freeroute(new);
		break;
	case Roverlaps:
		addnode(f, &p->tree.mid, new);
		break;
	}

	balancetree(cur);
}

/*
 *  find node matching r
 */
static Route**
looknode(Route **cur, Route *r)
{
	Route *p;

	for(;;){
		p = *cur;
		if(p == nil)
			return nil;
		switch(rangecompare(r, p)){
		case Rcontains:
			return nil;
		case Rpreceeds:
			cur = &p->tree.left;
			break;
		case Rfollows:
			cur = &p->tree.right;
			break;
		case Roverlaps:
			cur = &p->tree.mid;
			break;
		case Requals:
			if((p->tree.type & Rifc) == 0 && !matchroute(r, p))
				return nil;
			return cur;
		}
	}
}

static Route*
looknodematch(Route *r, int (*match)(Route*, void*), void *arg)
{
	Route *x;

	if(r == nil)
		return nil;
	if((x = looknodematch(r->tree.mid, match, arg)) != nil)
		return x;
	if((x = looknodematch(r->tree.left, match, arg)) != nil)
		return x;
	if((x = looknodematch(r->tree.right, match, arg)) != nil)
		return x;
	if((*match)(r, arg))
		return r;
	return nil;
}

static int
matchtag(Route *r, void *arg)
{
	char *tag = (char*)arg;

	if(r->tree.type & Rifc)
		return 0;
	return tag == nil || strncmp(tag, r->tree.tag, sizeof(r->tree.tag)) == 0;
}

static int
matchifc(Route *r, void *arg)
{
	Ipifc *ifc = (Ipifc*)arg;

	return r->tree.ifc == ifc;
}


#define	V4H(a)	((a&0x07ffffff)>>(32-Lroot-5))
#define	V6H(a)	(((a)[0]&0x80000000)>>(32-Lroot) | ((a)[(IPllen/2)-1]&(0xffff>>(16-Lroot))))

static void
routeadd(Fs *f, Route *r)
{
	Route **h, **e, *p, *q;
	if(r->tree.type & Rv4)
		return;

	{
		h = &f->v6root[V6H(r->v6.address)];
		e = &f->v6root[V6H(r->v6.endaddress)];
	}

	for(; h <= e; h++) {
		p = allocroute(r->tree.type);

		p->tree.ifc = r->tree.ifc;
		p->tree.ifcid = r->tree.ifcid;

			memmove(&p->v6, &r->v6, sizeof(r->v6));

		memmove(p->tree.tag, r->tree.tag, sizeof(r->tree.tag));

		addnode(f, h, p);
		while((p = f->queue) != nil) {
			f->queue = p->tree.mid;
			q = p->tree.left;
			freeroute(p);
			walkadd(f, h, q);
		}
	}

		v6routegeneration++;
}

static void
routedel(Fs *f, Route *r)
{
	Route **h, **e, **l, *p, *q;
	if(r->tree.type & Rv4)
		return;

	{
		h = &f->v6root[V6H(r->v6.address)];
		e = &f->v6root[V6H(r->v6.endaddress)];
	}

	for(; h <= e; h++) {
		if((l = looknode(h, r)) == nil)
			continue;
		p = *l;
		if(--(p->tree.ref) != 0)
			continue;
		*l = nil;
		addqueue(&f->queue, p->tree.left);
		addqueue(&f->queue, p->tree.mid);
		addqueue(&f->queue, p->tree.right);
		freeroute(p);

		while((p = f->queue) != nil) {
			f->queue = p->tree.mid;
			q = p->tree.left;
			freeroute(p);
			walkadd(f, h, q);
		}
	}

		v6routegeneration++;
}

static Route
mkroute(uchar *a, uchar *mask, uchar *s, uchar *smask, uchar *gate, int type, Ipifc *ifc, uchar ifcid, char *tag)
{
	ulong x, y;
	Route r;
	int h;

	assert(ifc != nil);

	memset(&r, 0, sizeof(r));

	r.tree.type = type;
	r.tree.ifc = ifc;
	r.tree.ifcid = ifcid;

	if(type & Rv4)
		panic("mkroute: ipv4 disabled");
	{
		for(h = 0; h < IPllen; h++){
			x = nhgetl(a+4*h);
			y = nhgetl(mask+4*h);
			r.v6.address[h] = x & y;
			r.v6.endaddress[h] = x | ~y;

			x = nhgetl(s+4*h);
			y = nhgetl(smask+4*h);
			if(y != 0)
				r.tree.type |= Rsrc;
			r.v6.source[h] = x & y;
			r.v6.endsource[h] = x | ~y;
		}

		memmove(r.v6.gate, gate, IPaddrlen);
	}

	if(tag != nil)
		strncpy(r.tree.tag, tag, sizeof(r.tree.tag));

	return r;
}

void
addroute(Fs *f, uchar *a, uchar *mask, uchar *s, uchar *smask, uchar *gate, int type, Ipifc *ifc, char *tag)
{
	Route r = mkroute(a, mask, s, smask, gate, type, ifc, ifc->ifcid, tag);
	wlock(&routelock);
	routeadd(f, &r);
	wunlock(&routelock);
}

void
delroute(Fs *f, uchar *a, uchar *mask, uchar *s, uchar *smask, uchar *gate, int type, Ipifc *ifc, char *tag)
{
	Route r = mkroute(a, mask, s, smask, gate, type, ifc, ifc->ifcid, tag);
	wlock(&routelock);
	routedel(f, &r);
	wunlock(&routelock);
}

static void
flushroutematch(Fs *f, int (*match)(Route*, void*), void *arg)
{
	Route r, *x;
	int h;

	wlock(&routelock);
	for(h = 0; h < nelem(f->v6root); h++)
		while((x = looknodematch(f->v6root[h], match, arg)) != nil){
			memmove(&r, x, sizeof(RouteTree) + sizeof(V6route));
			routedel(f, &r);
		}
	wunlock(&routelock);
}

/*
 * flushrouteifc:
 *   remove all routes associated with ifc (wlock()'ed).
 */
void
flushrouteifc(Fs *f, Ipifc *ifc)
{
	flushroutematch(f, matchifc, ifc);
	ifc->ifcid++;	/* invalidate all routes */
}

/*
 * v4lookup, v6lookup:
 *  lookup a route to destination address a from source address s
 *  and return the route. returns nil if no route was found.
 *  an optional Routehint can be passed in rh to cache the lookup.
 *
 *  for v4lookup, addresses are in 4 byte format.
 */
/*
 * Plan2001: skip, if not nil, is a medium whose interfaces the route must
 * not go out of - a tunnel's own (outer) packets must not be routed into
 * it (#W, ip/devwg.c).  The route found is left in rh, for ipoput4/6.
 */

static Route*
v6lookup0(Fs *f, uchar *a, uchar *s, Routehint *rh, Medium *skip)
{
	ulong la[IPllen], ls[IPllen];
	ulong x, y;
	Route *p, *q;
	Ipifc *ifc;
	int h;

	if(isv4(s))
		return nil;

	if(skip == nil && rh != nil
	&& rh->rgen == v6routegeneration
	&& (q = rh->r) != nil
	&& (ifc = q->tree.ifc) != nil
	&& q->tree.ifcid == ifc->ifcid
	&& q->tree.ref > 0)
		return q;

	q = nil;
	for(h = 0; h < IPllen; h++){
		la[h] = nhgetl(a+4*h);
		ls[h] = nhgetl(s+4*h);
	}

	rlock(&routelock);
	for(p = f->v6root[V6H(la)]; p != nil;){
		for(h = 0; h < IPllen; h++){
			x = la[h];
			y = p->v6.address[h];
			if(x == y)
				continue;
			if(x < y){
				p = p->tree.left;
				goto next;
			}
			break;
		}
		for(h = 0; h < IPllen; h++){
			x = la[h];
			y = p->v6.endaddress[h];
			if(x == y)
				continue;
			if(x > y){
				p = p->tree.right;
				goto next;
			}
			break;
		}
		if((p->tree.type & Rsrc) && (skip == nil || ipcmp(s, IPnoaddr) != 0)){
			for(h = 0; h < IPllen; h++){
				x = ls[h];
				y = p->v6.source[h];
				if(x == y)
					continue;
				if(x < y){
					p = p->tree.mid;
					goto next;
				}
				break;
			}
			for(h = 0; h < IPllen; h++){
				x = ls[h];
				y = p->v6.endsource[h];
				if(x == y)
					continue;
				if(x > y){
					p = p->tree.mid;
					goto next;
				}
				break;
			}
		}
		if(skip == nil || p->tree.ifc == nil || p->tree.ifc->m != skip)
			q = p;
		p = p->tree.mid;
next:		;
	}
	if(rh != nil){
		rh->r = q;
		rh->rgen = v6routegeneration;
	}
	runlock(&routelock);
	return q;
}

Route*
v6lookup(Fs *f, uchar *a, uchar *s, Routehint *rh)
{
	return v6lookup0(f, a, s, rh, nil);
}

Route*
v6lookupskip(Fs *f, uchar *a, uchar *s, Routehint *rh, Medium *skip)
{
	return v6lookup0(f, a, s, rh, skip);
}

/*
 * v4source, v6source:
 *  lookup a route to destination address a and also find
 *  a suitable source address s on the outgoing interface.
 *  return the route on success or nil when no route
 *  was found.
 *
 *  for v4source, addresses are in 4 byte format.
 */

Route*
v6source(Fs *f, uchar *a, uchar *s)
{
	uchar src[IPaddrlen];
	int splen, h;
	ulong x, y, la[IPllen];
	Route *p, *q;
	Ipifc *ifc;

	for(h = 0; h < IPllen; h++)
		la[h] = nhgetl(a+4*h);
again:
	q = nil;
	rlock(&routelock);
	for(p = f->v6root[V6H(la)]; p != nil;){
		for(h = 0; h < IPllen; h++){
			x = la[h];
			y = p->v6.address[h];
			if(x == y)
				continue;
			if(x < y){
				p = p->tree.left;
				goto next;
			}
			break;
		}
		for(h = 0; h < IPllen; h++){
			x = la[h];
			y = p->v6.endaddress[h];
			if(x == y)
				continue;
			if(x > y){
				p = p->tree.right;
				goto next;
			}
			break;
		}
		ifc = p->tree.ifc;
		if(!canrlock(&ifc->rwlk)){
			int gen = v6routegeneration;
			runlock(&routelock);
			rlock(&ifc->rwlk);
			rlock(&routelock);
			if(v6routegeneration != gen){
				runlock(&routelock);
				runlock(&ifc->rwlk);
				goto again;
			}
		}
		splen = 0;
		if(p->tree.type & Rsrc){
			/* calculate local prefix length for source specific routes */
			for(h = 0; h < IPllen; h++){
				hnputl(src+4*h, p->v6.source[h]);
				if((x = ~(p->v6.endsource[h] ^ p->v6.source[h])) != ~0UL){
					for(; x & 0x80000000UL; x <<= 1)
						splen++;
					break;
				}
				splen += 32;
			}
		}
		if(!ipv6local(ifc, src, splen, a)){
			runlock(&ifc->rwlk);
			p = p->tree.mid;
			continue;
		}
		ipmove(s, src);
		runlock(&ifc->rwlk);
		q = p;
		p = p->tree.mid;
next:		;
	}
	runlock(&routelock);
	return q;
}
