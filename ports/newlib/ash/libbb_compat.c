#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/times.h>
#include <pwd.h>
#include <stdarg.h>
extern void xfunc_die(void);
void *test_ptr_to_statics;
void *xmalloc(size_t n){void *p=malloc(n?n:1);if(!p)xfunc_die();return p;}
void *xzalloc(size_t n){void *p=xmalloc(n);memset(p,0,n);return p;}
void *xrealloc(void *p,size_t n){p=realloc(p,n?n:1);if(!p)xfunc_die();return p;}
void *xmemdup(const void *p,size_t n){return memcpy(xmalloc(n),p,n);}
char *xstrdup(const char *s){return strcpy(xmalloc(strlen(s)+1),s);}
void xwrite(int fd,const void *p,size_t n){while(n){int k=write(fd,p,n);if(k<=0)xfunc_die();p=(const char*)p+k;n-=k;}}
int fflush_all(void){return fflush(NULL);}
void _exit_SUCCESS(void){_exit(0);}
int bb_putchar(int c){return putchar(c);}
char *bb_itoa(int n){static char b[32];snprintf(b,sizeof b,"%d",n);return b;}
char *bb_utoa(unsigned n){static char b[32];snprintf(b,sizeof b,"%u",n);return b;}
void xgettimeofday(struct timeval *v){if(gettimeofday(v,NULL))xfunc_die();}
clock_t times(struct tms *p){extern clock_t _times(struct tms *);return _times(p);}
long sysconf(int n){if(n==_SC_CLK_TCK)return 1000;errno=ENOSYS;return -1;}
uid_t geteuid(void){errno=ENOSYS;return (uid_t)-1;}
gid_t getgid(void){errno=ENOSYS;return (gid_t)-1;}
gid_t getegid(void){errno=ENOSYS;return (gid_t)-1;}
int getgroups(int n,gid_t *p){(void)n;(void)p;errno=ENOSYS;return -1;}
struct passwd *getpwnam(const char *s){(void)s;errno=ENOSYS;return NULL;}
mode_t umask(mode_t mask){(void)mask;errno=ENOSYS;return (mode_t)-1;}
unsigned string_array_len(char **p){unsigned n=0;while(p[n])++n;return n;}
