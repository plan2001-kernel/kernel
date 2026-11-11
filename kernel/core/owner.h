#ifndef PLAN2001_CORE_OWNER_H
#define PLAN2001_CORE_OWNER_H

#include "../../runtime/abi/vobj.h"

/*
 * Single-owner identity registry and registered-program launch.
 *
 * An OwnerID is an immutable 64-bit identity derived from the trusted
 * name at registration.  It is not a Unix uid/gid.  Kernel-internal
 * authority is OwnerID 0; user-visible named owners, including root, are
 * ordinary identities and never override another owner's objects.
 */
enum {
	OwnerNameMax	= 32,
	OwnerMax	= 64,
	OwnerProgramMax	= 32,
	OwnerProgramNameMax = 32,
};

enum {
	OwnerProgramAutostart	= 1<<0,
	OwnerProgramLaunchable	= 1<<1,
};

void	ownerinit(void);
uvlong	ownerroot(void);
int	ownernamed(uvlong id, char *name, int n);
uvlong	ownercreate(char *name, int n);
int	ownerprogrambootprepare(uvlong oid);
void	ownerprogrambootregister(void);
int	ownerinfo(uvlong id, unsigned int *flags, char *name, int n);

#endif
