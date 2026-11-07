/*
 * Native kernel keyboard interface.
 *
 * These entry points expose the existing PS/2 scan-code queue without a
 * Chan or Qid.  The caller must hold the exclusive claim returned by
 * kbdscancodeacquire() until kbdscancoderelease().  kbdscancodeacquire()
 * returns 0 when the claim was acquired, -1 when another caller already
 * holds it, and -2 when no usable initialized PS/2 controller path is
 * available.  It does not sleep or perform I/O.
 *
 * kbdscancoderead() fails immediately when the controller is unavailable;
 * otherwise it uses the ordinary queue blocking behaviour (it may sleep).
 *
 * Access control is deliberately not performed here: a syscall layer that
 * exposes these operations to a process must enforce its own authority
 * (for example iseve()) before calling them.
 */
#ifndef PLAN2001_KBDAPI_H
#define PLAN2001_KBDAPI_H

int	kbdscancodeacquire(void);	/* 0 acquired, -1 already held, -2 unavailable */
void	kbdscancoderelease(void);
long	kbdscancoderead(void*, long);
void	kbdsetleds(int);

#endif
