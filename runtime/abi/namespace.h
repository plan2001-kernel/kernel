/* NamespaceView object ABI records. All fields are fixed-width little endian. */
#ifndef PLAN2001_NAMESPACE_H
#define PLAN2001_NAMESPACE_H
/* Include the environment's integer typedefs before this header. */
typedef struct { unsigned char volume[16]; unsigned long long space,oid; } NsIdentity;
typedef struct { NsIdentity object; unsigned int rights,reserved; } NsVisible;
enum { NS_REPL=0, NS_BEFORE=1, NS_AFTER=2 };
#endif
