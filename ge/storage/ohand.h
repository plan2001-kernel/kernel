/* Native GE/O volume and one-object capability rights.
 * No namespace role, Pgrp, SpaceID or OContext object exists. */
#ifndef PLAN2001_OHAND_H
#define PLAN2001_OHAND_H
enum {
    Oread=1, Owrite=2, Ocreate=4, Ocommit=8, Oexecute=16,
    Odelegate=32, Oall=63,
};
typedef struct Ohand Ohand;
struct Ohand {
    Ref ref;
    Ohand *origin; /* pinned origin of a derived GE/O grant */
    QLock gate;      /* admission + revocation serialization */
    void *sp;        /* opaque flat O-volume transaction handle */
    uvlong oid;      /* 0 for volume-wide, nonzero for one object */
    int rights, bootstrap, revoked, caps;
    char space[256]; /* transient bootstrap name, not on-disk SpaceID */
#ifdef PLAN2001_LIFECYCLE_DIAG
    ulong diagid;
#endif
};
void ohandput(Ohand*);
#endif
