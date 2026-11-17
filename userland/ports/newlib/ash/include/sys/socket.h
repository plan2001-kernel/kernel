/* Source compatibility only for upstream BusyBox libbb declarations.
 * Plan2001 offers NO ambient BSD sockets or kernel network namespace.
 * These types do not grant any socket facility.
 */
#ifndef PLAN2001_ASH_NEWLIB_SYS_SOCKET_H
#define PLAN2001_ASH_NEWLIB_SYS_SOCKET_H
#include <sys/types.h>
typedef unsigned int socklen_t;
typedef unsigned short sa_family_t;
struct sockaddr { sa_family_t sa_family; char sa_data[14]; };
struct sockaddr_storage { sa_family_t ss_family; char __padding[126]; };
#define AF_UNSPEC 0
#define AF_UNIX 1
#define AF_INET 2
#define AF_INET6 10
#define SOCK_STREAM 1
#define SOCK_DGRAM 2
#define SOCK_RAW 3
#define SOCK_RDM 4
#define SOCK_SEQPACKET 5
/* Declarations permit compiling unused upstream libbb sections. No socket
 * implementation is supplied; selecting a networking applet must still fail
 * the native link until a real authorized service exists. */
int socket(int,int,int);
int bind(int,const struct sockaddr *,socklen_t);
int listen(int,int);
ssize_t sendto(int,const void *,size_t,int,const struct sockaddr *,socklen_t);
#endif
