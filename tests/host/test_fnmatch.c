#include <assert.h>
#include <stdio.h>
#include "fnmatch.h"
static void match(const char *p,const char *s,int flags,int want)
{
    assert(fnmatch(p,s,flags)==want);
}
int main(void)
{
    match("abc","abc",0,0);
    match("*","",0,0);
    match("*","abc",0,0);
    match("a?c","abc",0,0);
    match("a?c","ac",0,FNM_NOMATCH);
    match("a*b*c","axybyc",0,0);
    match("[a-z]*","hello",0,0);
    match("[!a-z]*","9cache",0,0);
    match("[a-c]","z",0,FNM_NOMATCH);
    match("File*","file1",FNM_CASEFOLD,0);
    match("a\\*","a*",0,0);
    match("a\\*","abc",0,FNM_NOMATCH);
    match("dir/*","dir/file",FNM_PATHNAME,0);
    match("dir/*","dir/sub/file",FNM_PATHNAME,FNM_NOMATCH);
    match("*","a/b",FNM_PATHNAME,FNM_NOMATCH);
    match("*",".hidden",FNM_PERIOD,FNM_NOMATCH);
    match(".*",".hidden",FNM_PERIOD,0);
    match("sub/*","sub/.secret",FNM_PATHNAME|FNM_PERIOD,FNM_NOMATCH);
    match("a/*","a/b/c",FNM_LEADING_DIR|FNM_PATHNAME,0);
    match("[abc","a",0,FNM_NOMATCH);
    match("[[:alpha:]]","a",0,FNM_NOMATCH); /* unsupported named class */
    puts("PASS: bounded freestanding fnmatch wildcards, brackets, escapes, flags");
    return 0;
}
