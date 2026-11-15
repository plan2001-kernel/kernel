/* Process-local Present View Descriptor (PVD), initial memory-only Unix view.
 * No native GE/O/ObjectSpace authority is inferred from these labels.
 * This header deliberately needs no hosted C library or kernel ABI header.
 */
#ifndef PLAN2001_USERLAND_PVD_H
#define PLAN2001_USERLAND_PVD_H

#define PVD_MAX_NODES 64u
#define PVD_MAX_DEPTH 16u
#define PVD_LABEL_MAX 47u
#define PVD_PATH_MAX 256u

enum {
    PVD_OK = 0,
    PVD_ENOENT = -2,
    PVD_EACCES = -13,
    PVD_EIO = -5,
    PVD_EEXIST = -17,
    PVD_EINVAL = -22,
    PVD_ENAMETOOLONG = -36,
    PVD_ELOOP = -40,
    PVD_EAMBIG = -76,
    PVD_ENOSPC = -28
};

typedef struct PvdNode {
    unsigned int parent;
    char label[PVD_LABEL_MAX + 1];
} PvdNode;

typedef struct Pvd {
    PvdNode nodes[PVD_MAX_NODES];
    unsigned int count;
    unsigned int depth;
    unsigned int path[PVD_MAX_DEPTH];
} Pvd;

typedef int (*PvdEmit)(void *context, const char *label);

void pvd_init(Pvd *view);
int pvd_mkdir_memory(Pvd *view, const char *path);
int pvd_bootstrap(Pvd *view);
int pvd_chdir(Pvd *view, const char *path);
int pvd_getcwd(const Pvd *view, char *buffer, unsigned long capacity);
int pvd_list(const Pvd *view, const char *path, PvdEmit emit, void *context);
const char *pvd_error(int code);
#endif
