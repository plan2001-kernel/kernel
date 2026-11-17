/* Native GE/O backend. Volume is an externally delegated capability Handle.
 * The API never calls ObjOpenVolume and never guesses a privileged OID.
 */
#ifndef PLAN2001_UXGEO_H
#define PLAN2001_UXGEO_H
#include "uxfs.h"
typedef struct { int volume; } UxGeo;
int uxgeo_bind(UxGeo *geo, int volume, UxStore *store);
#endif
