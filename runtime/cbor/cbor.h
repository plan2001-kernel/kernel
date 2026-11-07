/* RFC 8949 bounded, allocation-free subset. No GE or interpreter dependency. */
#ifndef PLAN2001_CBOR_H
#define PLAN2001_CBOR_H
#include <stddef.h>
#include <stdint.h>
#define CB_MAX_NODES 512
#define CB_MAX_DEPTH 16
#define CB_MAX_BYTES (1024u*1024u)
enum { CB_OK=0,CB_INVALID=-1,CB_TRUNCATED=-2,CB_UNSUPPORTED=-3,
 CB_UTF8=-4,CB_DUPLICATE=-5,CB_LIMIT=-6,CB_OVERFLOW=-7,CB_INTEGRITY=-8 };
enum { CB_UINT,CB_NEG,CB_BYTES,CB_TEXT,CB_ARRAY,CB_MAP,CB_BOOL,CB_NULL };
/* Indices are one-based; zero means no node. CB_NEG.integer is -1-value.
 * String spans are borrowed and must remain live for the document lifetime.
 * Map keys are scalars. Containers use len items/pairs and linked children.
 */
typedef struct { unsigned type; uint64_t integer; const unsigned char *bytes;
 size_t len; unsigned child,next; } CbValue;
typedef struct { unsigned used,root; CbValue values[CB_MAX_NODES]; } CbDoc;
void cb_init(CbDoc *);
unsigned cb_new(CbDoc *,unsigned);
int cb_array_add(CbDoc *,unsigned,unsigned);
int cb_map_add(CbDoc *,unsigned,unsigned,unsigned);
int cb_decode(CbDoc *,const void *,size_t);
/* Core deterministic encoding, RFC 8949 4.2.1 (bytewise key order).
 * NULL/zero output measures size. No partial output is valid on failure.
 */
int cb_encode(const CbDoc *,void *,size_t,size_t *);
/* Same encoding with one named field omitted from the root map only. */
int cb_encode_without(const CbDoc *,const char *,void *,size_t,size_t *);
unsigned cb_field(const CbDoc *,const char *);
int cb_utf8(const unsigned char *,size_t);
#endif
