/* A bounded userspace-only directory projection. Nodes are *names*, not
 * native ObjectSpace/GE/O capabilities. A path never authorizes an object.
 * The selected parent stack is part of each process-local PVD.
 */
#include "pvd.h"

static int equal(const char *a, const char *b)
{
    while (*a && *b && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

static void copy_label(char *out, const char *in, unsigned int n)
{
    unsigned int i;
    for (i = 0; i < n; ++i)
        out[i] = in[i];
    out[n] = 0;
}

void pvd_init(Pvd *view)
{
    unsigned int i;
    if (!view)
        return;
    view->count = 1;
    view->depth = 1;
    view->path[0] = 0;
    view->nodes[0].parent = 0;
    view->nodes[0].label[0] = 0;
    for (i = 1; i < PVD_MAX_NODES; ++i) {
        view->nodes[i].parent = 0;
        view->nodes[i].label[0] = 0;
    }
}

/* Resolve without changing cwd on failure; optionally create exactly the
 * final component. The parser is bounded, and .. cannot escape this root.
 */
static int walk(Pvd *view, const char *name, unsigned int *stack,
                unsigned int *depth, int create)
{
    unsigned int i, n, d, len = 0, start, partlen, parent, found, matches;
    char label[PVD_LABEL_MAX + 1];
    int last, created = 0;
    if (!view || !name || !name[0] || !stack || !depth)
        return PVD_EINVAL;
    while (name[len] && len <= PVD_PATH_MAX)
        ++len;
    if (len >= PVD_PATH_MAX)
        return PVD_ENAMETOOLONG;
    d = name[0] == '/' ? 1 : view->depth;
    if (!d || d > PVD_MAX_DEPTH)
        return PVD_EINVAL;
    for (i = 0; i < d; ++i)
        stack[i] = view->path[i];
    for (i = 0; i < len;) {
        while (i < len && name[i] == '/')
            ++i;
        if (i == len)
            break;
        start = i;
        while (i < len && name[i] != '/')
            ++i;
        partlen = i - start;
        if (partlen > PVD_LABEL_MAX)
            return PVD_ENAMETOOLONG;
        copy_label(label, name + start, partlen);
        while (i < len && name[i] == '/')
            ++i;
        last = i == len;
        if (equal(label, "."))
            continue;
        if (equal(label, "..")) {
            if (d > 1)
                --d;
            continue;
        }
        if (d == PVD_MAX_DEPTH)
            return PVD_ELOOP;
        parent = stack[d - 1];
        found = 0;
        matches = 0;
        for (n = 1; n < view->count; ++n) {
            if (view->nodes[n].parent == parent &&
                equal(view->nodes[n].label, label)) {
                found = n;
                ++matches;
            }
        }
        if (matches > 1)
            return PVD_EAMBIG;
        if (!matches) {
            if (!create || !last)
                return PVD_ENOENT;
            if (view->count >= PVD_MAX_NODES)
                return PVD_ENOSPC;
            found = view->count++;
            view->nodes[found].parent = parent;
            copy_label(view->nodes[found].label, label, partlen);
            created = 1;
        } else if (create && last) {
            return PVD_EEXIST;
        }
        stack[d++] = found;
    }
    if (create && !created)
        return PVD_EINVAL;
    *depth = d;
    return PVD_OK;
}

int pvd_mkdir_memory(Pvd *view, const char *path)
{
    unsigned int stack[PVD_MAX_DEPTH], depth;
    return walk(view, path, stack, &depth, 1);
}

int pvd_bootstrap(Pvd *view)
{
    static const char *const dirs[] = {
        "/bin", "/dev", "/etc", "/home", "/tmp",
        "/usr", "/usr/bin", "/var"
    };
    unsigned int i;
    int status;
    if (!view)
        return PVD_EINVAL;
    pvd_init(view);
    for (i = 0; i < sizeof(dirs) / sizeof(dirs[0]); ++i) {
        status = pvd_mkdir_memory(view, dirs[i]);
        if (status != PVD_OK)
            return status;
    }
    return PVD_OK;
}

int pvd_chdir(Pvd *view, const char *path)
{
    unsigned int stack[PVD_MAX_DEPTH], depth, i;
    int status = walk(view, path, stack, &depth, 0);
    if (status != PVD_OK)
        return status;
    for (i = 0; i < depth; ++i)
        view->path[i] = stack[i];
    view->depth = depth;
    return PVD_OK;
}

int pvd_getcwd(const Pvd *view, char *buffer, unsigned long capacity)
{
    unsigned int i, j, used = 0;
    const char *name;
    if (!view || !buffer || !capacity || !view->depth)
        return PVD_EINVAL;
    if (capacity < 2)
        return PVD_ENAMETOOLONG;
    buffer[used++] = '/';
    for (i = 1; i < view->depth; ++i) {
        name = view->nodes[view->path[i]].label;
        if (i > 1) {
            if (used + 1 >= capacity)
                return PVD_ENAMETOOLONG;
            buffer[used++] = '/';
        }
        for (j = 0; name[j]; ++j) {
            if (used + 1 >= capacity)
                return PVD_ENAMETOOLONG;
            buffer[used++] = name[j];
        }
    }
    buffer[used] = 0;
    return PVD_OK;
}

int pvd_list(const Pvd *view, const char *path, PvdEmit emit, void *context)
{
    unsigned int stack[PVD_MAX_DEPTH], depth, i;
    int status;
    if (!view || !emit)
        return PVD_EINVAL;
    status = walk((Pvd *)view, path && path[0] ? path : ".",
                  stack, &depth, 0);
    if (status != PVD_OK)
        return status;
    for (i = 1; i < view->count; ++i) {
        if (view->nodes[i].parent == stack[depth - 1]) {
            if (emit(context, view->nodes[i].label) != 0)
                return PVD_EIO;
        }
    }
    return PVD_OK;
}

const char *pvd_error(int code)
{
    switch (code) {
    case PVD_ENOENT: return "no such directory";
    case PVD_EACCES: return "permission denied";
    case PVD_EIO: return "output failed";
    case PVD_EEXIST: return "already exists";
    case PVD_EINVAL: return "invalid path";
    case PVD_ENAMETOOLONG: return "name or path too long";
    case PVD_ELOOP: return "view depth limit";
    case PVD_EAMBIG: return "ambiguous binding";
    case PVD_ENOSPC: return "memory view full";
    default: return "unknown error";
    }
}
