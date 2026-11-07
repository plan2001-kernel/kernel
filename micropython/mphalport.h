// Plan2001: the console is raw for the REPL's line editor and cooked
// while code runs (p9hal.c), so kbdfs's interrupt (Ctrl-C, Del) is a
// note: KeyboardInterrupt
void mp_hal_set_interrupt_char(int c);
