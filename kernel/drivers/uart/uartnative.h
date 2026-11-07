/*
 * Native kernel UART interface.
 *
 * Port ids are discovery-local indexes (0 .. uartnports()-1).  uartnreset()
 * performs the initialization that previously ran as the #t Dev reset.  A
 * native open shares the existing Uart.opens accounting; the last close
 * performs the usual drain and disable.  Console and special UARTs are
 * reserved for their existing kernel owners and are rejected by the native
 * open path.
 */
#ifndef PLAN2001_UARTNATIVE_H
#define PLAN2001_UARTNATIVE_H

typedef struct Uartcfg Uartcfg;
enum {
	Uartbaud = 1, Uartbits = 2, Uartstop = 4, Uartparity = 8,
	Uartfifo = 16, Uartrts = 32, Uartdtr = 64,
	Uarthupdsr = 128, Uarthupdcd = 256,
};
enum { Uartparitynone = 0, Uartparityodd = 1, Uartparityeven = 2 };
struct Uartcfg {
	int set;
	int baud, bits, stop, parity, fifo, rts, dtr, hupdsr, hupdcd;
};
void uartnreset(void);
int  uartnports(void);
void uartnopen(int id, int mode);    /* mode OREAD/OWRITE/ORDWR; errors via error() */
void uartnclose(int id);
long uartnread(int id, void*, long);
long uartnwrite(int id, void*, long);
void uartnctl(int id, Uartcfg*);
long uartnstatus(int id, char*, long);

#endif
