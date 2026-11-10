#ifndef PLAN2001_ENVAPI_H
#define PLAN2001_ENVAPI_H
int  envnativeget(char *name, void *buf, long n, long *len);
int  envnativeset(char *name, void *val, long len);
int  envnativedelete(char *name);
long envnativelist(void *buf, long n, long *need);
#ifdef PLAN2001_LIFECYCLE_DIAG
int  envfaulttake(char *name, char *want);
#endif
#endif
