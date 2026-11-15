/* Minimal freestanding declarations for BusyBox 1.36.1 true/false/echo.
 * This intentionally does NOT claim to implement libbb or POSIX.
 * The upstream applet .c files are compiled without modifications.
 * Original BusyBox code remains subject to GPL-2.0-or-later.
 */
#ifndef PLAN2001_BUSYBOX_LIBBB_H
#define PLAN2001_BUSYBOX_LIBBB_H
typedef unsigned long size_t;
typedef long ssize_t;

#define MAIN_EXTERNALLY_VISIBLE __attribute__((visibility("default")))
#define UNUSED_PARAM __attribute__((unused))
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#define NULL ((void *)0)
#define ENABLE_FEATURE_FANCY_ECHO 0

extern int errno;
extern const char bb_msg_write_error[];
extern size_t strlen(const char *s);
extern void free(void *p);
extern void *xmalloc(size_t size);
extern char *stpcpy(char *dest, const char *src);
extern ssize_t full_write(int fd, const void *buf, size_t count);
extern ssize_t full_read(int fd, void *buf, size_t count);
extern int p2001_session_probe(void);
extern void p2001_session_finish(void);
extern ssize_t p2001_session_read(int fd, void *buf, size_t count);
extern ssize_t p2001_session_write(int fd, const void *buf, size_t count);
extern void bb_simple_perror_msg(const char *message);
/* Upstream echo.c parses escapes only in ENABLE_FEATURE_FANCY_ECHO mode;
 * the initial port disables this path, but its source still needs a prototype. */
extern int bb_process_escape_sequence(const char **cursor);
#endif
