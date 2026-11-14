/* Upstream Newlib includes this header from termios.h, but the generic
 * x86_64-unknown-elf install does not provide one. Types are for source
 * compatibility; real F5 sessions are UXPIPE, not POSIX PTYs.
 */
#ifndef PLAN2001_ASH_NEWLIB_SYS_TERMIOS_H
#define PLAN2001_ASH_NEWLIB_SYS_TERMIOS_H
#include <sys/types.h>
typedef unsigned int tcflag_t;
typedef unsigned char cc_t;
typedef unsigned int speed_t;
#define NCCS 32
struct termios {
    tcflag_t c_iflag, c_oflag, c_cflag, c_lflag;
    cc_t c_cc[NCCS];
    speed_t c_ispeed, c_ospeed;
};
#define TCSANOW 0
#define TCSADRAIN 1
#define TCSAFLUSH 2
#define ICANON 0x0002
#define ECHO 0x0008
#define ISIG 0x0001
#define VINTR 0
#define VQUIT 1
#define VERASE 2
#define VKILL 3
#define VEOF 4
#define VMIN 6
#define VTIME 5
#define B9600 9600
int tcgetattr(int fd,struct termios *attr);
int tcsetattr(int fd,int action,const struct termios *attr);
int tcdrain(int fd);
int tcflush(int fd,int selector);
#endif
