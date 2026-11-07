#ifndef PLAN2001_OENV_H
#define PLAN2001_OENV_H
#include "cbor.h"
/* Hash two spans in order, producing SHA-256. No allocation or GE dependency
 * is imposed on the caller's crypto provider. Return zero on success. */
typedef int (*OeHash)(const void *,size_t,const void *,size_t,unsigned char[32]);
/* pack adds v=1 if absent, always inserts/replaces sha256 and mutates doc.
 * digest storage must stay live through use of doc; output does not alias
 * strings borrowed by doc. Both calls require a caller-owned scratch buffer.
 */
int oe_pack(CbDoc *,void *,size_t,size_t *,unsigned char[32],OeHash);
int oe_unpack(CbDoc *,const void *,size_t,void *,size_t,int,OeHash);
int oe_validate(const CbDoc *);
#endif
