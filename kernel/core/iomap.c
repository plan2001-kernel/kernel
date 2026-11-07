#include "u.h"
#include "../core/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

typedef struct IOMap IOMap;
struct IOMap
{
	IOMap	*next;
	ulong	start;
	ulong	end;
	char	reserved;
	char	tag[13];
};

static struct
{
	Lock	lk;	/* named to avoid promoted `m` collision */
	IOMap	*m;
	IOMap	*free;
	IOMap	maps[32];	/* some initial free maps */

	ulong	mask;
} iomap;
#define IOMAPLK	(&iomap.lk)

static void
insert(IOMap **l, ulong start, ulong end, char *tag, int reserved)
{
	IOMap *m;

	m = iomap.free;
	if(m != nil)
		iomap.free = m->next;
	else
		m = malloc(sizeof(IOMap));
	if(m == nil)
		panic("ioalloc: out of memory");

	m->next = *l;
	m->start = start;
	m->end = end;
	m->reserved = reserved;

	strncpy(m->tag, tag, sizeof(m->tag)-1);
	m->tag[sizeof(m->tag)-1] = 0;

	*l = m;
}

/*
 * Reserve a range to be ioalloced later.
 * If port is -1, look for a free range above 0x400.
 */
int
ioreserve(ulong port, ulong size, ulong align, char *tag)
{
	if(port == -1)
		return ioreservewin(0x400, -0x400 & iomap.mask, size, align, tag);
	else
		return ioreservewin(port, size, size, align, tag);
}

/*
 * Find a free region of length "size" within the window [port, port+win)
 * and reserve it to be ioalloced later.
 */
int
ioreservewin(ulong port, ulong win, ulong size, ulong align, char *tag)
{
	IOMap *m, **l;

	if(win == 0 || port & ~iomap.mask || (~port & iomap.mask) < (win-1 & iomap.mask))
		return -1;

	size = (size + ~iomap.mask) & iomap.mask;
	if(size == 0 || size > win)
		return -1;

	if(align){
		if((align & (align-1)) != 0)
			return -1;
		align--;
	}

	win += port;
	port = (port+align) & ~align;
	if(port >= win || win - port < size)
		return -1;

	lock(IOMAPLK);
	for(l = &iomap.m; (m = *l) != nil; l = &m->next){
		if(m->end <= port)
			continue;
		if(m->start > port && m->start - port >= size)
			break;
		port = m->end;
		port = (port+align) & ~align;
		if(port >= win || win - port < size){
			unlock(IOMAPLK);
			return -1;
		}
	}
	insert(l, port, port + size, tag, 1);
	unlock(IOMAPLK);

	return port;
}

/*
 * Alloc some io port space and remember who it was
 * alloced to. If port == -1, find a free region.
 */
int
ioalloc(ulong port, ulong size, ulong align, char *tag)
{
	IOMap *m, **l;

	if(port == -1)
		port = ioreserve(port, size, align, tag);

	size = (size + ~iomap.mask) & iomap.mask;
	if(size == 0 || port & ~iomap.mask || (~port & iomap.mask) < (size-1 & iomap.mask))
		return -1;

	lock(IOMAPLK);
	for(l = &iomap.m; (m = *l) != nil; l = &m->next){
		if(m->end <= port)
			continue;
		if(m->start > port && m->start - port >= size)
			break;
		if(m->reserved && m->start <= port && m->end - port >= size){
			if(m->end - port > size)
				insert(&m->next, port + size, m->end, m->tag, 1);
			if(m->start < port){
				insert(l, m->start, port, m->tag, 1);
				l = &(*l)->next;
			}
			*l = m->next;
			m->next = iomap.free;
			iomap.free = m;
			break;
		}
		/* if this is ACPI resource, dont bother */
		if(*m->tag == '\\')
			break;
		print("ioalloc: %lux - %lux %s: clashes with: %lux - %lux %s\n",
			port, port+size-1, tag,
			m->start, m->end-1, m->tag);
		unlock(IOMAPLK);
		return -1;
	}
	insert(l, port, port + size, tag, 0);
	unlock(IOMAPLK);

	return port;
}

void
iofreetag(ulong port, char *tag)
{
	IOMap *m, **l;

	if(port & ~iomap.mask)
		return;

	lock(IOMAPLK);
	for(l = &iomap.m; (m = *l) != nil; l = &m->next){
		if(m->start == port){
			/* don't touch ACPI when no tag given */
			if(tag == nil && *m->tag == '\\')
				continue;
			/* if tag is specified, it must match */
			if(tag != nil && strcmp(tag, m->tag) != 0)
				continue;
			*l = m->next;
			m->next = iomap.free;
			iomap.free = m;
			break;
		}
		if(m->start > port)
			break;
	}
	unlock(IOMAPLK);
}

void
iofree(ulong port)
{
	iofreetag(port, nil);
}

int
iounused(ulong start, ulong end)
{
	IOMap *m;

	if(start & ~iomap.mask || end < start)
		return 0;

	for(m = iomap.m; m != nil; m = m->next){
		if(m->end <= start)
			continue;
		if(m->start >= end)
			break;
		if(!m->reserved)
			return 0;
	}
	return 1;
}

/*
 * Initialize the io space port map.
 * The mask argument defines the valid bits of
 * a port address, so different architectures
 * might have different sizes and alignments.
 */
void
iomapinit(ulong mask)
{
	int i;

	assert(mask != 0 && (mask >> 31) == 0);

	for(i = 0; i < nelem(iomap.maps)-1; i++)
		iomap.maps[i].next = &iomap.maps[i+1];
	iomap.maps[i].next = nil;
	iomap.free = iomap.maps;
	iomap.mask = mask;
}
