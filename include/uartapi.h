#ifndef _UARTAPI_H_
#define _UARTAPI_H_

typedef struct Uartcfg Uartcfg;
enum {
	Uartbaud = 1, Uartbits = 2, Uartstop = 4, Uartparity = 8,
	Uartfifo = 16, Uartrts = 32, Uartdtr = 64,
	Uarthupdsr = 128, Uarthupdcd = 256,
};
enum { Uartparitynone = 0, Uartparityodd = 1, Uartparityeven = 2 };
enum { UARTOREAD = 0, UARTOWRITE = 1, UARTORDWR = 2 };

struct Uartcfg {
	int set;
	int baud, bits, stop, parity, fifo, rts, dtr, hupdsr, hupdcd;
};

/*
 * UART access is reached only through the five-operation OBJ ABI:
 * OBJINVOKE(ObjOpDeviceList) reports ObjDeviceUart discovery records with the
 * port index in selector0; OBJOPEN(ObjOpenDevice) opens one through an
 * authorized catalog Handle with a versioned mode option;
 * OBJINVOKE(UartCtl/UartStatus) controls it, and OBJCLOSE releases it.  The
 * historical UART* numbers and ObjOpenUart are reserved and dispatch to
 * nothing.
 */

#endif
