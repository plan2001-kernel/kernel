/* Unit test the actual F5 builtin dispatcher, with a captured native stdout. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pvd_cli.h"
#include "libbb.h"

static char output[4096];
static size_t used;
ssize_t full_write(int fd, const void *buf, size_t n)
{
    assert(fd == 1);
    assert(used + n + 1 < sizeof(output));
    memcpy(output + used, buf, n);
    used += n;
    output[used] = 0;
    return (ssize_t)n;
}

static void run(Pvd *v, const char *line, const char *expect)
{
    used = 0;
    output[0] = 0;
    assert(pvd_cli_run(v, line) == 0);
    if (strcmp(output, expect) != 0)
        fprintf(stderr, "line=%s expected=%s got=%s\n", line, expect, output);
    assert(strcmp(output, expect) == 0);
}

int main(void)
{
    Pvd v;
    assert(pvd_bootstrap(&v) == PVD_OK);
    run(&v, "pwd", "/\n");
    run(&v, "ls", "bin\ndev\netc\nhome\ntmp\nusr\nvar\n");
    run(&v, "cd /usr/bin", "");
    run(&v, "pwd", "/usr/bin\n");
    run(&v, "ls ..", "bin\n");
    run(&v, "cd ../../..", "");
    run(&v, "pwd", "/\n");
    run(&v, "cd /missing", "cd: no such directory\n");
    run(&v, "pwd", "/\n");
    run(&v, "cd", "cd: path required\n");
    run(&v, "pwd foo", "pwd: unexpected argument\n");
    run(&v, "ls /absent", "ls: no such directory\n");
    run(&v, "hello", "command not found: hello\n");
    run(&v, "ls /etc", "");
    puts("PASS: actual F5 pwd/cd/ls builtin outputs");
    return 0;
}
