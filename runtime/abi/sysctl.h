/*
 * SYSCTL: typed system/process configuration and introspection.
 *
 * SYSCTL never creates children, waits for children, transfers object
 * payload, mutates namespace bindings or issues device commands.  A
 * selector, PID, OID, operation number or requested rights mask never
 * creates authority.
 *
 * The call is sysctl(scope, operation, SysctlReq *request, long size).
 * "request" is one fixed-width, versioned SysctlReq record; nested pointers
 * are validated by the kernel before use.  Unknown operation numbers are
 * rejected.  The record is copied back to the caller with result and aux
 * filled in.
 */
#ifndef PLAN2001_ABI_SYSCTL_H
#define PLAN2001_ABI_SYSCTL_H

#define SYSCTL_ABI_VERSION 1

/* Scope is explicit: own-process introspection may not reach another
 * process, and privileged system operations keep their existing checks. */
enum {
	SysctlScopeOwnProcess	= 0,
	SysctlScopeSystem	= 1,
};

enum {
	/* system status words; data is a bounded output buffer */
	SysctlSnapshot		= 1,
	/* caller's own Handle table; data is a VobjListRec array */
	SysctlHandleList	= 2,
	/* process environment; name/namelen plus data value */
	SysctlEnvGet		= 3,
	SysctlEnvSet		= 4,
	SysctlEnvDelete		= 5,
	SysctlEnvList		= 6,
	/* privileged bootstrap/administrative O volume activation */
	SysctlVolumeOpen	= 7,
	/* read-only own-process CPU affinity / memory budget introspection */
	SysctlProcessInfo	= 8,
};

typedef struct SysctlReq {
	unsigned int		version;	/* SYSCTL_ABI_VERSION */
	unsigned int		flags;		/* must be zero */
	void			*name;		/* env name bytes */
	unsigned int		namelen;	/* NUL-free name length */
	unsigned int		reserved;	/* must be zero */
	void			*data;		/* value/records/output bytes */
	unsigned long long	datalen;	/* data capacity in bytes */
	unsigned long long	arg0;		/* reserved, must be zero */
	unsigned long long	arg1;		/* reserved, must be zero */
	long long		result;		/* out: operation result */
	unsigned long long	aux;		/* out: secondary count */
} SysctlReq;

/* Own-process CPU affinity and memory budget.  There is no safe runtime
 * setter; both values are chosen atomically at spawn/fork time. */
typedef struct SysctlProcInfo {
	long long		pid;
	unsigned long long	cpu;		/* affinity, ~0ULL = unpinned */
	unsigned long long	membudget;	/* 0 = unlimited */
} SysctlProcInfo;

long	sysctl(unsigned int scope, unsigned int operation, SysctlReq *req,
		long size);

/* libc convenience wrappers; the kernel routes remain SYSCTL. */
long	proc_info(SysctlProcInfo *out);

#endif /* PLAN2001_ABI_SYSCTL_H */
