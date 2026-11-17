/* Native Plan2001 cat stdin filter. It receives explicit UXPIPE regions
 * and no pathname or GE/O privilege. NOT upstream BusyBox cat.c. */
#include "libbb.h"
int p2001_cat_main(int argc, char **argv)
{
    unsigned char bytes[256];
    ssize_t n;
    (void)argv;
    if(argc!=1) return 64;
    for(;;) {
        n=full_read(STDIN_FILENO,bytes,sizeof(bytes));
        if(n==0) return 0;
        if(n<0) return 1;
        if(full_write(STDOUT_FILENO,bytes,(size_t)n)!=n) return 1;
    }
}
