/* Real Plan2001 F5 supervisor child linked with upstream Newlib.
 * Verify public read()/write() dispatch through EXACT inherited UXPIPE
 * capabilities. There is no console handle, synthetic ring, or Linux libc.
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "plan2001_full.h"

static int report(int fd, const char *text)
{
    size_t len = strlen(text);
    return write(fd, text, len) == (ssize_t)len ? 0 : -1;
}

static int failure(const char *message)
{
    (void)report(2, "PLAN2001-NEWLIB-DELEGATED-FAIL:");
    (void)report(2, message);
    (void)report(2, "\n");
    return 1;
}

int main(int argc, char **argv)
{
    static const char ready[] = "PLAN2001-NEWLIB-SUPERVISED-READY\n";
    static const char out[] = "PLAN2001-NEWLIB-DELEGATED-STDOUT-PASS\n";
    static const char err[] = "PLAN2001-NEWLIB-DELEGATED-STDERR-PASS\n";
    static const char input[] = "pipeok";
    static const char reply[] = "PLAN2001-NEWLIB-DELEGATED-STDIN-PASS:pipeok\n";
    struct stat st;
    char received[sizeof input] = {0};
    unsigned i = 0;
    char c;
    ssize_t n;

    if(argc != 2 || !argv || strcmp(argv[0], "unix-child") != 0 ||
       !argv[1] || argv[1][0] != '0')
        return 1;
    /* p9_start already bound the verified inherited grants before main.
     * A missing binding makes the following FIFO checks fail closed. */
    if(fstat(0, &st) != 0 || !S_ISFIFO(st.st_mode))
        return failure("stdin fstat");
    if(fstat(1, &st) != 0 || !S_ISFIFO(st.st_mode))
        return failure("stdout fstat");
    if(fstat(2, &st) != 0 || !S_ISFIFO(st.st_mode))
        return failure("stderr fstat");
    errno = 0;
    if(isatty(1) != 0 || errno != ENOTTY)
        return failure("stdout unexpectedly claims native console");
    if(report(1, out) || report(2, err) || report(1, ready))
        return failure("delegated output");
    for(;;) {
        n = read(0, &c, 1);
        if(n != 1) return failure("delegated input");
        if(c == '\r' || c == '\n') break;
        if(i >= sizeof input - 1)
            return failure("input too long");
        received[i++] = c;
    }
    if(strcmp(received, input) != 0)
        return failure("stdin payload corrupted");
    if(report(1, reply))
        return failure("stdin acknowledgement");
    /* Retain the F5 display while the QEMU monitor reads its pixels.
     * The supervisor closes the session when the child completes; without
     * this hold the final output can vanish before the VGA assertion. */
    (void)sleep(8);
    /* Explicitly publish EOF on both output streams before exiting.
     * A disappeared writer mapping is NOT a valid substitute for EOF. */
    if(close(0) != 0 || close(1) != 0 || close(2) != 0)
        return 1;
    return 0;
}
