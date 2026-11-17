#ifndef PLAN2001_PROJECT_NATIVE_H
#define PLAN2001_PROJECT_NATIVE_H
#include <stddef.h>
#include <stdint.h>
#include "record.h"
#include "obj.h"
#include "vobj.h"
#include "../unix/uxobject.h"
/* A Handle is the authority; identity fields are only descriptive. */
typedef struct {int handle;uint32_t kind,rights;ProjectId id;uint64_t version,position_after;} PjObject;
int pj_from_handle(PjObject *,int);
void pj_close(PjObject *);
int pj_call(int,ObjProjectReq *,void *,size_t);
int pj_blob(PjObject *,unsigned char **,size_t *,size_t *,size_t *);
int pj_empty(const UxMeta *,unsigned char **,size_t *);
int pj_resolve(PjObject *,const char *,PjObject *);
int pj_create(PjObject *,const char *,unsigned,const void *,size_t,PjObject *);
int pj_create_attenuated(PjObject *,const char *,unsigned,const void *,size_t,unsigned,PjObject *);
int pj_link(PjObject *,const char *,PjObject *);
int pj_remove(PjObject *,const char *,int);
int pj_rename(PjObject *,const char *,PjObject *,const char *,int);
int pj_metadata(PjObject *,UxMeta *);
int pj_metadata_size(PjObject *,UxMeta *,size_t *);
long pj_read(PjObject *,void *,size_t,uint64_t);
long pj_write(PjObject *,const void *,size_t,uint64_t,int);

int pj_list(PjObject *,int (*)(void *,const ProjectEntry *),void *);
#endif
