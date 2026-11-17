/* Stage-3 F5 shell entry. unix-init supplies an initialized PVD.
 * It is deliberately a same-process builtin interpreter; no Unix exec,
 * environment or POSIX fd table is claimed.
 */
#include "libbb.h"
#include "f5_shell.h"
#include "pvd_cli.h"
#include "../unix/uxshell.h"
#include "../unix/uxgeo.h"
#include "../unix/uxservice.h"
#include "../unix/uxexternal.h"
#include "../unix/uxrpc_external.h"

extern int plan2001_busybox_dispatch(int argc, char **argv);

static int equal(const char *a,const char *b)
{
    while(*a && *a==*b) {++a;++b;}
    return *a==*b;
}
static int shell_external(void *context,const char *command,
                          char *const *argv,unsigned int argc,
                          void *output,unsigned int capacity,
                          unsigned int *size)
{
    UxExternal *e=(UxExternal *)context;
    if(equal(command,"uxfile"))
        return uxrpc_external_run(e->service,e->executable_volume,
                                  e->executable_oid,argv,argc,
                                  output,capacity,size);
    return uxexternal_ls(context,command,argv,argc,output,capacity,size);
}

static int write_bytes(const char *bytes, size_t length)
{
    return full_write(1, bytes, length) == (ssize_t)length ? 0 : -1;
}

static int write_text(const char *text)
{
    return write_bytes(text, strlen(text));
}

static int shell_output(void *unused, const void *data, unsigned int size)
{
    (void)unused;
    return write_bytes((const char *)data, size);
}
static int shell_error(const char *line, int status)
{
    const char *start = line;
    const char *end;
    if (status == PVD_OK) return 0;
    while (*start == ' ' || *start == '\t') ++start;
    end = start;
    while (*end && *end != ' ' && *end != '\t' &&
           *end != '|' && *end != '>') ++end;
    if (end == start) start = "command", end = start + 7;
    if (write_bytes(start, (size_t)(end-start)) < 0 ||
        write_text(": ") < 0 ||
        write_text(pvd_error(status)) < 0 ||
        write_text("\n") < 0) return -1;
    return 0;
}
static int prompt(const Pvd *view)
{
    char path[PVD_PATH_MAX];
    if (pvd_getcwd(view, path, sizeof(path)) != PVD_OK)
        return -1;
    if (write_text("unix:") < 0 || write_text(path) < 0 ||
        write_text("$ ") < 0)
        return -1;
    return 0;
}

int p2001_f5_shell(Pvd *view, void *context)
{
    char *echo_args[] = {
        "busybox", "echo", "PLAN2001-BUSYBOX-F5-READY", 0
    };
    char line[128], root[2], ch;
    static const char read_marker[] = "PLAN2001-BUSYBOX-F5-READ:";
    static UxService service;
    UxFs *fs=&service.fs;
    UxShellIO io = {0};
    io.emit = shell_output;
    int result;
    const UxF5Storage *storage = (const UxF5Storage *)context;
    UxGeo geo;
    UxStore store;
    UxExternal external;
    unsigned int used = 0;
    long n;
    if (!view || pvd_getcwd(view, root, sizeof(root)) != PVD_OK ||
        root[0] != '/' || root[1] != 0)
        return -1;
    uxservice_init(&service, view);
    if (storage) {
        if (!!storage->executable_volume != !!storage->executable_oid)
            return -1; /* no incomplete authority context */
        if (!!storage->volume != !!storage->manifest ||
            (storage->restore && !storage->manifest))
            return -1;
        if (storage->manifest) {
            if (uxgeo_bind(&geo, storage->volume, &store) < 0)
                return -1;
            if (uxservice_bind(&service, &store, storage->manifest,
                               storage->restore) != PVD_OK)
                return -1;
        }
        if (storage->executable_volume) {
            external.fs = fs;
            external.service = &service;
            external.executable_volume = storage->executable_volume;
            external.executable_oid = storage->executable_oid;
            io.context = &external;
            io.external = shell_external;
            io.external_ls = 1;
            io.pipeline = uxexternal_pipeline;
        }
    }
    if (plan2001_busybox_dispatch(3, echo_args) != 0)
        return -1;
    if (write_text("PLAN2001-PVD-READY\n") < 0 ||
        write_text("PLAN2001-UNIX-INIT-ROOT:/\n") < 0 ||
        write_text("PLAN2001-UNIX-SHELL-READY\n") < 0 ||
        prompt(view) < 0)
        return -1;
    for (;;) {
        n = full_read(0, &ch, 1);
        if (n != 1)
            return -1;
        if (ch == '\r' || ch == '\n') {
            line[used] = 0;
            if (write_text("\n") < 0 ||
                write_text(read_marker) < 0 ||
                write_bytes(line, used) < 0 ||
                write_text("\n") < 0)
                return -1;
            result = uxservice_command(&service, line, &io);
            if (shell_error(line, result) < 0 || prompt(view) < 0)
                return -1;
            used = 0;
        } else if (ch == 8 || ch == 127) {
            if (used) {
                --used;
                if (write_text("\b \b") < 0)
                    return -1;
            }
        } else if ((unsigned char)ch >= 32 && (unsigned char)ch < 127) {
            if (used < sizeof(line) - 1) {
                line[used++] = ch;
                if (write_bytes(&ch, 1) < 0)
                    return -1;
            }
        }
    }
}
