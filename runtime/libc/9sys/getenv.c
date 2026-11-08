#include <u.h>
#include <libc.h>
#include <envapi.h>

/* Native process environment; no pathname or retired FD fallback. */
char*
getenv(char *name)
{
    char *s;
    long n, len;

    if(name == nil || name[0] == 0 || strlen(name) >= 128){
        werrstr("bad env name");
        return nil;
    }
    n = 0;
    if(envget(strlen(name), name, nil, 0, &n) < 0 || n < 0 || n > 512*1024)
        return nil;
    s = malloc(n+1);
    if(s == nil) return nil;
    len = n;
    if(envget(strlen(name), name, s, n, &len) < 0 || len < 0 || len > n){
        free(s);
        return nil;
    }
    n = len;
    while(n > 0 && s[n-1] == 0) n--;
    s[n] = 0;
    while(--n >= 0) if(s[n] == 0) s[n] = ' ';
    return s;
}
