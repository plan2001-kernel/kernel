#ifndef _ENVAPI_H_
#define _ENVAPI_H_

long	envget(int, char*, void*, long, long*);
long	envset(int, char*, void*, long);
long	envdelete(int, char*);
long	envlist(void*, long, long*);

#endif
