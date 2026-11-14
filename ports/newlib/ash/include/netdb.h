/* Only the type declarations required for compiling BusyBox libbb.h.
 * No DNS, getaddrinfo, or socket service exists on this Plan2001 target.
 */
#ifndef PLAN2001_ASH_NEWLIB_NETDB_H
#define PLAN2001_ASH_NEWLIB_NETDB_H
#include <sys/socket.h>
struct hostent {
    char *h_name;
    char **h_aliases;
    int h_addrtype, h_length;
    char **h_addr_list;
};
struct addrinfo {
    int ai_flags, ai_family, ai_socktype, ai_protocol;
    socklen_t ai_addrlen;
    struct sockaddr *ai_addr;
    char *ai_canonname;
    struct addrinfo *ai_next;
};
#endif
