/* NS2-NS4 pathless amd64 ABI. All fields fixed-width little endian. */
#ifndef PLAN2001_NAMESPACE_H
#define PLAN2001_NAMESPACE_H
/* Include the environment's integer typedefs before this header. */
typedef struct { unsigned char volume[16]; unsigned long long space,oid; } NsIdentity;
typedef struct { NsIdentity object; unsigned int rights,reserved; } NsVisible;
typedef struct { unsigned long long handle,oid; } NsAuthority;
typedef struct { unsigned long long buffer,capacity,count; } NsOutput;
enum { NS_REPL=0, NS_BEFORE=1, NS_AFTER=2,
 NS_FIND=1, NS_GET=2, NS_TAGS=3, NS_ADD=4, NS_REMOVE=5,
 NS_CREATE=6, NS_BOOT=256 };
/* The namespace view operations use typed OBJ handles. */
long obindx(int,unsigned long long,char *,int,int);
long oquery(int,void *,void *,int,void *);
#endif
