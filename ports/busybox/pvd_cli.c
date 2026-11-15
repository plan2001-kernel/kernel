/* F5 acceptance builtins for the process-local, memory-only Unix view.
 * The native console transport is in session.c; no path is a Handle.
 */
#include "libbb.h"
#include "pvd_cli.h"
extern int strcmp(const char *a, const char *b);

static int out(const char *text)
{
    size_t len = strlen(text);
    return full_write(1, text, len) == (ssize_t)len ? 0 : -1;
}

static int emit(void *ctx UNUSED_PARAM, const char *label)
{
    if (out(label) < 0 || out("\n") < 0)
        return -1;
    return 0;
}

static int whitespace(char c)
{
    return c == ' ' || c == '\t';
}

int pvd_cli_run(Pvd *view, const char *line)
{
    char command[16];
    char argument[PVD_PATH_MAX];
    unsigned int c = 0, a = 0;
    char cwd[PVD_PATH_MAX];
    const char *p = line;
    int status;
    if (!view || !line)
        return -1;
    while (whitespace(*p))
        ++p;
    if (!*p)
        return 0;
    while (*p && !whitespace(*p)) {
        if (c + 1 >= sizeof(command))
            return out("command: name too long\n");
        command[c++] = *p++;
    }
    command[c] = 0;
    while (whitespace(*p))
        ++p;
    while (*p && !whitespace(*p)) {
        if (a + 1 >= sizeof(argument))
            return out("path: too long\n");
        argument[a++] = *p++;
    }
    argument[a] = 0;
    while (whitespace(*p))
        ++p;
    if (*p)
        return out("usage: too many arguments\n");

    if (strcmp(command, "pwd") == 0) {
        if (a)
            return out("pwd: unexpected argument\n");
        status = pvd_getcwd(view, cwd, sizeof(cwd));
        if (status == PVD_OK)
            return out(cwd) < 0 ? -1 : out("\n");
    } else if (strcmp(command, "cd") == 0) {
        if (!a)
            return out("cd: path required\n");
        status = pvd_chdir(view, argument);
        if (status == PVD_OK)
            return 0;
    } else if (strcmp(command, "ls") == 0) {
        status = pvd_list(view, a ? argument : ".", emit, 0);
        if (status == PVD_OK)
            return 0;
    } else {
        return out("command not found: ") < 0 ? -1 :
               out(command) < 0 ? -1 : out("\n");
    }
    return out(command) < 0 ? -1 :
           out(": ") < 0 ? -1 :
           out(pvd_error(status)) < 0 ? -1 : out("\n");
}
