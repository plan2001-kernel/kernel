/* This ordinary spawned child links *only* Newlib libc and the target
 * syscall adapter; exit() must call the native EXITS boundary.
 */
#include <stdlib.h>

int main(int argc, char **argv)
{
    if(argc != 2 || argv[1] == 0)
        exit(99);
    if(argv[1][0] == '0' && argv[1][1] == 0)
        exit(0);
    if(argv[1][0] == '7' && argv[1][1] == 0)
        exit(7);
    exit(98);
}
