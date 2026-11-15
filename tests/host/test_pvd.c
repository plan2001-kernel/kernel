/* Host unit tests exercise the identical freestanding PVD source used in QEMU. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pvd.h"

static char lines[2048];
static unsigned int len;

static int emit(void *unused, const char *name)
{
    size_t n = strlen(name);
    (void)unused;
    if (len + n + 2 > sizeof(lines))
        return -1;
    memcpy(lines + len, name, n);
    len += (unsigned int)n;
    lines[len++] = '\n';
    lines[len] = 0;
    return 0;
}

static void cwd(Pvd *v, const char *want)
{
    char path[PVD_PATH_MAX];
    assert(pvd_getcwd(v, path, sizeof(path)) == PVD_OK);
    assert(strcmp(path, want) == 0);
}

static void ls(Pvd *v, const char *path, const char *want)
{
    len = 0;
    lines[0] = 0;
    assert(pvd_list(v, path, emit, 0) == PVD_OK);
    assert(strcmp(lines, want) == 0);
}

int main(void)
{
    Pvd v;
    char label[64];
    char small[2];
    unsigned int i;
    assert(pvd_bootstrap(&v) == PVD_OK);
    cwd(&v, "/");
    ls(&v, 0, "bin\ndev\netc\nhome\ntmp\nusr\nvar\n");
    ls(&v, "/usr", "bin\n");
    ls(&v, "/bin", "");
    assert(pvd_chdir(&v, "/usr/bin") == PVD_OK);
    cwd(&v, "/usr/bin");
    assert(pvd_chdir(&v, "..") == PVD_OK);
    cwd(&v, "/usr");
    assert(pvd_chdir(&v, "./bin/../bin//") == PVD_OK);
    cwd(&v, "/usr/bin");
    assert(pvd_chdir(&v, "../../../../../../") == PVD_OK);
    cwd(&v, "/");
    assert(pvd_chdir(&v, "usr//bin/.") == PVD_OK);
    cwd(&v, "/usr/bin");
    assert(pvd_chdir(&v, "/missing") == PVD_ENOENT);
    cwd(&v, "/usr/bin");
    assert(pvd_chdir(&v, "") == PVD_EINVAL);
    assert(pvd_chdir(&v, "../bin/nonexistent") == PVD_ENOENT);
    cwd(&v, "/usr/bin");
    assert(pvd_getcwd(&v, small, sizeof(small)) == PVD_ENAMETOOLONG);
    assert(pvd_mkdir_memory(&v, "/tmp/new") == PVD_OK);
    ls(&v, "/tmp", "new\n");
    assert(pvd_mkdir_memory(&v, "/tmp/new") == PVD_EEXIST);
    assert(pvd_mkdir_memory(&v, "/unknown/sub") == PVD_ENOENT);
    assert(pvd_mkdir_memory(&v, "/tmp/..") == PVD_EINVAL);
    for (i = 0; i < 48; ++i) label[i] = 'x';
    label[48] = 0;
    assert(pvd_chdir(&v, label) == PVD_ENAMETOOLONG);
    assert(pvd_mkdir_memory(&v, "/tmp/f") == PVD_OK);
    assert(pvd_mkdir_memory(&v, "/tmp/f/g") == PVD_OK);
    assert(pvd_chdir(&v, "/tmp/f/g") == PVD_OK);
    cwd(&v, "/tmp/f/g");
    /* Deliberately inject two same-label edges: lookup must fail closed. */
    assert(v.count + 1 < PVD_MAX_NODES);
    v.nodes[v.count].parent = v.nodes[v.count - 1].parent;
    strcpy(v.nodes[v.count].label, v.nodes[v.count - 1].label);
    ++v.count;
    assert(pvd_chdir(&v, "/tmp/f/g") == PVD_EAMBIG);
    cwd(&v, "/tmp/f/g");
    puts("PASS: PVD memory root/cwd, dotdot confinement, ls, mutable tree, errors and ambiguous bindings");
    return 0;
}
