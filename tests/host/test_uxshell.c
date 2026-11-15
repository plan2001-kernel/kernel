#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "uxshell.h"
static Pvd pvd;
static UxFs fs;
static char received[4096];
static unsigned int pos;
static unsigned int external_calls;
static int emit(void *ctx, const void *buf, unsigned int n)
{
    (void)ctx;
    assert(pos+n < sizeof(received));
    memcpy(received+pos,buf,n);
    pos+=n;
    received[pos]=0;
    return 0;
}
static int external(void *ctx, const char *name, char *const *argv,
                    unsigned int argc, void *buf, unsigned int capacity,
                    unsigned int *size)
{
    const char data[]="external-ls\n";
    (void)ctx;
    assert(!strcmp(name,"ls") && !strcmp(argv[0],"ls") && argc==1);
    assert(capacity >= sizeof(data)-1);
    memcpy(buf,data,sizeof(data)-1);
    *size=sizeof(data)-1;
    ++external_calls;
    return 0;
}
static unsigned int native_calls;
static int pipeline(void *ctx,char *const *left,unsigned int lc,
                    char *const *right,unsigned int rc,
                    void *dst,unsigned int cap,unsigned int *n)
{
    static const char result[]="native-pipe-output\n";
    (void)ctx;
    assert(lc==2 && rc==1);
    assert(!strcmp(left[0],"echo") && !strcmp(left[1],"hello"));
    assert(!strcmp(right[0],"cat"));
    assert(cap>=sizeof(result)-1);
    memcpy(dst,result,sizeof(result)-1);
    *n=sizeof(result)-1;
    ++native_calls;
    return 0;
}
static void reset(void) {pos=0; received[0]=0;}
int main(void)
{
    UxShellIO io={0};
    io.emit=emit;
    assert(pvd_bootstrap(&pvd)==0);
    uxfs_init(&fs,&pvd);
    assert(uxshell_eval(&fs, "echo test > /tmp/file", &io)==0);
    assert(pos==0);
    assert(uxshell_eval(&fs, "cat /tmp/file", &io)==0);
    assert(!strcmp(received,"test\n"));
    reset();
    assert(uxshell_eval(&fs, "echo 'two words' >> /tmp/file",&io)==0);
    assert(uxshell_eval(&fs, "cat /tmp/file | cat",&io)==0);
    assert(!strcmp(received,"test\ntwo words\n"));
    reset();
    assert(uxshell_eval(&fs, "echo pipeline | cat > /tmp/piped",&io)==0);
    assert(uxshell_eval(&fs, "cat /tmp/piped",&io)==0);
    assert(!strcmp(received,"pipeline\n"));
    reset();
    assert(uxshell_eval(&fs, "mkdir /tmp/sub",&io)==0);
    assert(uxshell_eval(&fs, "cp /tmp/file /tmp/sub/a",&io)==0);
    assert(uxshell_eval(&fs, "mv /tmp/sub/a /tmp/sub/b",&io)==0);
    assert(uxshell_eval(&fs, "cd /tmp/sub",&io)==0);
    assert(uxshell_eval(&fs, "ls",&io)==0);
    assert(!strcmp(received,"b\n"));
    reset();
    assert(uxshell_eval(&fs, "pwd",&io)==0);
    assert(!strcmp(received,"/tmp/sub\n"));
    reset();
    assert(uxshell_eval(&fs, "rm b",&io)==0);
    assert(uxshell_eval(&fs, "ls",&io)==0);
    assert(pos==0);
    assert(uxshell_eval(&fs, "echo broken |",&io)==PVD_EINVAL);
    assert(uxshell_eval(&fs, "echo 'unclosed",&io)==PVD_EINVAL);
    assert(uxshell_eval(&fs, "cat /tmp/missing",&io)==PVD_ENOENT);
    assert(uxshell_eval(&fs, "echo nope > /missing/file",&io)==PVD_ENOENT);
    assert(uxshell_eval(&fs, "echo hi | cat | cat",&io)==PVD_EINVAL);
    /* Stage 6: external ls uses the explicitly installed process executor.
     * The callback is the only authority, not a path string or numeric PID.
     */
    io.external=external; io.external_ls=1;
    assert(uxshell_eval(&fs,"ls",&io)==0);
    assert(!strcmp(received,"external-ls\n"));
    assert(external_calls==1);
    reset();
    io.pipeline=pipeline;
    assert(uxshell_eval(&fs,"echo hello | cat",&io)==0);
    assert(!strcmp(received,"native-pipe-output\n") && native_calls==1);
    puts("PASS: commands, quotes, >, >>, |, errors, separate-executor request");
    return 0;
}
