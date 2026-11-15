/* Bounded command parser and file/stream adapters.
 * A pipe is a one-shot userland byte buffer, not a kernel Pipe or claim
 * of concurrent POSIX pipe/fork semantics. Unsupported syntax is rejected.
 */
#include "uxshell.h"
#define SH_TOKENS 20u
#define SH_LINE 256u
typedef struct {
    unsigned char buf[UXFS_DATA_MAX];
    unsigned int used;
} Output;

static unsigned int slen(const char *s)
{
    unsigned int n = 0;
    while (s[n]) ++n;
    return n;
}
static int eq(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static int output(Output *o, const void *p, unsigned int n)
{
    unsigned int i;
    const unsigned char *q = (const unsigned char *)p;
    if (n > UXFS_DATA_MAX-o->used) return PVD_ENOSPC;
    for (i = 0; i < n; ++i) o->buf[o->used+i] = q[i];
    o->used += n;
    return PVD_OK;
}
static int text(Output *o, const char *s)
{
    return output(o, s, slen(s));
}
static int listing(void *opaque, const void *data, unsigned int size)
{
    Output *o = (Output *)opaque;
    int rc = output(o, data, size);
    return rc ? -1 : output(o, "\n", 1) ? -1 : 0;
}
static int command(UxFs *fs, char **argv, unsigned int argc,
                   const unsigned char *input, unsigned int input_size,
                   Output *out, const UxShellIO *io)
{
    char cwd[PVD_PATH_MAX];
    unsigned int i, n;
    int rc;
    if (!argc) return PVD_OK;
    if (eq(argv[0], "pwd")) {
        if (argc != 1) return PVD_EINVAL;
        rc = pvd_getcwd(fs->view, cwd, sizeof(cwd));
        return rc ? rc : text(out, cwd) ? PVD_ENOSPC : text(out, "\n");
    }
    if (eq(argv[0], "cd")) {
        if (argc != 2) return PVD_EINVAL;
        return pvd_chdir(fs->view, argv[1]);
    }
    if (eq(argv[0], "mkdir")) {
        if (argc != 2) return PVD_EINVAL;
        return uxfs_mkdir(fs, argv[1]);
    }
    if (eq(argv[0], "rm")) {
        if (argc != 2) return PVD_EINVAL;
        return uxfs_remove(fs, argv[1]);
    }
    if (eq(argv[0], "cp")) {
        if (argc != 3) return PVD_EINVAL;
        return uxfs_copy(fs, argv[1], argv[2]);
    }
    if (eq(argv[0], "mv")) {
        if (argc != 3) return PVD_EINVAL;
        return uxfs_move(fs, argv[1], argv[2]);
    }
    if (eq(argv[0], "echo")) {
        for (i = 1; i < argc; ++i) {
            if (i > 1 && output(out, " ", 1)) return PVD_ENOSPC;
            if (text(out, argv[i])) return PVD_ENOSPC;
        }
        return output(out, "\n", 1);
    }
    if (eq(argv[0], "true")) return PVD_OK;
    if (eq(argv[0], "false")) return -1;
    if (eq(argv[0], "cat")) {
        unsigned char buf[UXFS_DATA_MAX];
        if (argc == 1)
            return input ? output(out, input, input_size) : PVD_EINVAL;
        if (argc != 2) return PVD_EINVAL;
        rc = uxfs_read(fs, argv[1], buf, sizeof(buf), &n);
        return rc ? rc : output(out, buf, n);
    }
    if (eq(argv[0], "ls")) {
        if (argc > 2) return PVD_EINVAL;
        if (io && io->external_ls && io->external) {
            n = UXFS_DATA_MAX;
            rc = io->external(io->context, argv[0], argv, argc,
                              out->buf, n, &n);
            if (rc || n > UXFS_DATA_MAX) return PVD_EIO;
            out->used = n;
            return PVD_OK;
        }
        return uxfs_list(fs, argc == 2 ? argv[1] : ".",
                         listing, out);
    }
    if (io && io->external) {
        n = UXFS_DATA_MAX;
        rc = io->external(io->context, argv[0], argv, argc,
                          out->buf, n, &n);
        if (rc || n > UXFS_DATA_MAX) return PVD_EIO;
        out->used = n;
        return PVD_OK;
    }
    return PVD_ENOENT;
}
static int whitespace(char c)
{
    return c == ' ' || c == '\t';
}
/* Tokenizer retains word order and strips paired quotes. Accepts only
 * a single | and one terminal > or >>. No variable expansion/globbing.
 */
static int tokenize(const char *line, char scratch[SH_LINE],
                    char *argv[SH_TOKENS], unsigned int *count)
{
    unsigned int pos = 0, n = 0;
    char quote = 0, ch;
    int active = 0;
    while ((ch = *line++) != 0) {
        if (pos + 2 >= SH_LINE) return PVD_ENAMETOOLONG;
        if (quote) {
            if (ch == quote) quote = 0;
            else scratch[pos++] = ch;
            continue;
        }
        if (ch == '\'' || ch == '"') {
            if (!active) {
                if (n >= SH_TOKENS) return PVD_ENOSPC;
                argv[n++] = &scratch[pos];
                active = 1;
            }
            quote = ch;
            continue;
        }
        if (ch == '\\') {
            if (!*line) return PVD_EINVAL;
            if (!active) {
                if (n >= SH_TOKENS) return PVD_ENOSPC;
                argv[n++] = &scratch[pos];
                active = 1;
            }
            scratch[pos++] = *line++;
            continue;
        }
        if (whitespace(ch) || ch == '|' || ch == '>') {
            if (active) { scratch[pos++] = 0; active = 0; }
            if (ch == '|' || ch == '>') {
                if (n >= SH_TOKENS) return PVD_ENOSPC;
                argv[n++] = &scratch[pos];
                scratch[pos++] = ch;
                if (ch == '>' && *line == '>') {
                    scratch[pos++] = *line++;
                }
                scratch[pos++] = 0;
            }
            continue;
        }
        if (!active) {
            if (n >= SH_TOKENS) return PVD_ENOSPC;
            argv[n++] = &scratch[pos];
            active = 1;
        }
        scratch[pos++] = ch;
    }
    if (quote) return PVD_EINVAL;
    if (active) scratch[pos++] = 0;
    scratch[pos] = 0;
    *count = n;
    return PVD_OK;
}
int uxshell_eval(UxFs *fs, const char *line, const UxShellIO *io)
{
    char scratch[SH_LINE], *args[SH_TOKENS];
    unsigned int n, split = 0, redirect = 0, target = 0;
    unsigned int i, pipe_count = 0;
    Output left = {{0},0}, right = {{0},0};
    int append = 0, rc;
    if (!fs || !line || !io || !io->emit) return PVD_EINVAL;
    rc = tokenize(line, scratch, args, &n);
    if (rc || !n) return rc;
    for (i = 0; i < n; ++i) {
        if (eq(args[i], "|")) {
            if (split || redirect || !i || i+1 == n)
                return PVD_EINVAL;
            split = i;
            ++pipe_count;
        } else if (eq(args[i], ">") || eq(args[i], ">>")) {
            if (redirect || !i || i+2 != n) return PVD_EINVAL;
            redirect = i;
            target = i+1;
            append = eq(args[i], ">>");
        }
    }
    if (pipe_count && redirect && redirect <= split+1) return PVD_EINVAL;
    if (split && io->pipeline && eq(args[0],"echo") &&
        eq(args[split+1],"cat") &&
        (redirect ? redirect : n)==split+2) {
        unsigned int result_size=0;
        rc=io->pipeline(io->context,args,split,args+split+1,1,
                        right.buf,UXFS_DATA_MAX,&result_size);
        if(rc || result_size>UXFS_DATA_MAX) return PVD_EIO;
        right.used=result_size;
    } else if (split) {
        rc = command(fs, args, split, 0, 0, &left, io);
        if (rc) return rc;
        rc = command(fs, args+split+1,
                     (redirect ? redirect : n) - split - 1,
                     left.buf, left.used, &right, io);
    } else {
        rc = command(fs, args, redirect ? redirect : n,
                     0, 0, &right, io);
    }
    if (rc) return rc;
    if (redirect) {
        if (target >= n) return PVD_EINVAL;
        return uxfs_write(fs, args[target], right.buf, right.used, append);
    }
    return io->emit(io->context, right.buf, right.used) ? PVD_EIO : PVD_OK;
}
