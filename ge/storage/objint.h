/*
 * Private GE object engine internals (obj.c).  The includer must include
 * "gefs.h" first; this header does not include it.
 */
#ifndef GEFS_OBJINT_H
#define GEFS_OBJINT_H

enum {
	Objhdrksz	= 1,
	Objoidksz	= 1+8,
	Objksz		= 1+8+8+4,
	Objchunk	= 480,		/* < Inlmax */
	Objuser		= 256,		/* the header's user part */
	Objtomb		= 0xffffffff,	/* len of a tombstone */
	Objbatch	= 32,		/* messages with values to an upsert */
	Objclob		= 256,		/* clobbers to an upsert: > Bufspc */
	Objrhdr		= 1+8+8+8,	/* 'o' oid ver len */
	Objtxmax	= 64*MiB,	/* a transaction's data, an object's */
	Objyield	= 4,		/* upserts between objtxflush's yields */
};

typedef struct Ohdr Ohdr;
typedef struct Oent Oent;
typedef struct Otx Otx;

struct Ohdr {
	uvlong	n;
	uvlong	k;
	uvlong	w;
	int	nuser;
	char	user[Objuser];
};

struct Oent {
	uvlong	oid;
	u32int	nidx;
};

struct Otx {
	Gefs	*fs;		/* Plan2001: its volume */
	Otx	*next;		/* the active ones, under mutlk */
	Tree	*tree;		/* the volume's snapshot tree */

	/* the transaction: ver != 0 */
	uvlong	ver;
	Oent	*ent;
	int	nent;
	int	aent;
	vlong	bytes;
	int	newuser;
	int	nuser;
	char	user[Objuser];

	/* the write in progress: one object's chunks */
	uvlong	oid;
	u32int	idx;
	char	chunk[Objchunk];
	int	nchunk;

	/* messages for the next upsert */
	int	id;		/* the worker's epoch (yield), -1 none */
	int	nflush;		/* upserts since the last yield */
	Msg	mb[Objclob];
	char	kb[Objclob][Objksz];
	char	vb[Objbatch][Objchunk];
	int	nm;
};

/* The transaction engine and the native O API (obj.c). */
Tree*	txroot(Otx*);
int	gethdr(Tree*, Ohdr*);
uvlong	keyver(char*);
void	objtxsweep(Otx*, char*, int, int (*)(char*, int, void*), void*);
int	ospaceownerget(Ospace*, uvlong, uvlong*, int*);
void	ospaceownersetlegacy(uvlong);
void	objtxaddmsg(Otx*, int, char*, int, char*, int);
void	objtxbegin(Otx*);
void	objtxputchunk(Otx*);
void	objtxaddent(Otx*, uvlong, u32int);
void	objtxcommit(Otx*);
void	objtxabort(Otx*);
void	objtxflush(Otx*);
void	objtxend(Otx*);

#endif
