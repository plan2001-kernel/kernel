/* POSIX poll declarations for BusyBox source analysis.
 * A working poll() needs a descriptor/event service, not a header shim.
 */
#ifndef PLAN2001_ASH_NEWLIB_POLL_H
#define PLAN2001_ASH_NEWLIB_POLL_H
typedef unsigned long nfds_t;
struct pollfd { int fd; short events, revents; };
#define POLLIN 0x0001
#define POLLPRI 0x0002
#define POLLOUT 0x0004
#define POLLERR 0x0008
#define POLLHUP 0x0010
#define POLLNVAL 0x0020
int poll(struct pollfd *fds,nfds_t count,int timeout_ms);
#endif
