/* UJ1 native amd64 ABI additions that are not part of the object family.
 * The former TCP6* stream calls (108-113) are reserved and have no dispatch
 * entry; there is no kernel TCP6/UDP6 object interface.  Network protocols
 * live in userspace over raw device frames/ObjectFIFO.  The sixth snapshot
 * word is reserved and always zero. */
#ifndef PLAN2001_TCP6_H
#define PLAN2001_TCP6_H
typedef struct {
 unsigned long long version,cpus,physical_bytes,user_pool_bytes,volume_bytes;
 unsigned long long reserved,free_user_bytes;
} PlanSystemSnapshot;
long syssnapshot(PlanSystemSnapshot*);
#endif
