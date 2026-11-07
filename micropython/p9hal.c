#include "py/runtime.h"
#include "py/mphal.h"
#include "p9console.h"

int mp_hal_stdin_rx_chr(void) {
    int c = p9_console_read();
    // EOF exits even in a partial line or raw REPL; never spin on a closed
    // pipe.  A read error exits too, as a failure.
    if (c < 0) p9_console_exit(c == -2);
    return c;
}

mp_uint_t mp_hal_stdout_tx_strn(const char *str, size_t len) {
    if (p9_console_write(str, len) < 0) p9_console_exit(1);
    return len;
}

// pyexec: c >= 0 while code runs, -1 after
void mp_hal_set_interrupt_char(int c) {
    p9_console_raw(c < 0);
}

mp_uint_t mp_hal_ticks_ms(void) { return (mp_uint_t)p9_console_msec(); }
