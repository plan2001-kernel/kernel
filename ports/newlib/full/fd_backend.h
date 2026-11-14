/* Private backend identifiers never escape the userspace descriptor table. */
#ifndef PLAN2001_FD_BACKEND_H
#define PLAN2001_FD_BACKEND_H
#include <sys/types.h>
#include <sys/stat.h>
int p9_backend_read(int, char *, int);
int p9_backend_write(int, const char *, int);
int p9_backend_close(int);
int p9_backend_open(const char *, int, ...);
int p9_backend_fstat(int, struct stat *);
int p9_backend_isatty(int);
off_t p9_backend_lseek(int, off_t, int);
int p9_backend_stream(int,int [2],int *);
#endif
