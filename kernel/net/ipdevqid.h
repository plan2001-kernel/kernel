/*
 * Dormant legacy #I Qid directory-number encoding.  This header is not
 * included by any canonical or compiled source: the Chan-free IP core
 * (ipcore.c) now uses plain Fs/Proto indices and the native conversation
 * bound in ip.h.  Retained only as historical compatibility reference for
 * the inherited #I namespace; do not add new includes or users.
 */
enum
{
	Qtopdir=	1,		/* top level directory */
	Qtopbase,
	Qarp=		Qtopbase,
	Qbootp,
	Qndb,
	Qiproute,
	Qipselftab,
	Qlog,

	Qprotodir,			/* directory for a protocol */
	Qprotobase,
	Qclone=		Qprotobase,
	Qstats,
	Qtrans,

	Qconvdir,			/* directory for a conversation */
	Qconvbase,
	Qctl=		Qconvbase,
	Qdata,
	Qerr,
	Qlisten,
	Qlocal,
	Qremote,
	Qstatus,
	Qsnoop,

	Logtype=	5,
	Masktype=	(1<<Logtype)-1,
	Logconv=	12,
	Maskconv=	(1<<Logconv)-1,
	Shiftconv=	Logtype,
	Logproto=	8,
	Maskproto=	(1<<Logproto)-1,
	Shiftproto=	Logtype + Logconv,

	Nfs=		128,
};
#define TYPE(x) 	( ((ulong)(x).path) & Masktype )
#define CONV(x) 	( (((ulong)(x).path) >> Shiftconv) & Maskconv )
#define PROTO(x) 	( (((ulong)(x).path) >> Shiftproto) & Maskproto )
#define QID(p, c, y) 	( ((p)<<(Shiftproto)) | ((c)<<Shiftconv) | (y) )

#define DEVDOT -2
