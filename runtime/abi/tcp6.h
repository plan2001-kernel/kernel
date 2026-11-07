/* UJ1 native amd64 ABI additions that are not part of the object family.
 * The former TCP6* stream calls (108-113) are reserved and have no dispatch
 * entry; TCP listen/connect/accept/read/write/close now use the canonical
 * OBJ* object ABI (runtime/abi/obj.h). */
#ifndef PLAN2001_TCP6_H
#define PLAN2001_TCP6_H
typedef struct {
 unsigned long long version,cpus,physical_bytes,user_pool_bytes,volume_bytes;
 unsigned long long tcp_endpoints,free_user_bytes;
} PlanSystemSnapshot;
long syssnapshot(PlanSystemSnapshot*);
#endif
