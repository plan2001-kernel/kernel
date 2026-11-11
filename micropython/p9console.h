#ifndef P9CONSOLE_H
#define P9CONSOLE_H
#include <stddef.h>
long p9_console_attach(int inh, int outh, int diagh);
const char *p9_console_session_error(void);
int p9_console_init_native(void);
void p9_console_restore(void);
void p9_console_raw(int on);
int p9_console_read(void);
int p9_console_poll_interrupt(void);
int p9_console_write(const char *data, size_t size);
void p9_dprintf(char *fmt, ...);
void p9_console_native(int on);
void p9_console_probe(void);
unsigned long long p9_console_msec(void);
void p9_console_exit(int failed);
#endif
