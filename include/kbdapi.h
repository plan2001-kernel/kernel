#ifndef _KBDAPI_H_
#define _KBDAPI_H_

/*
 * PS/2 keyboard access is reached only through the five-operation OBJ ABI:
 * OBJOPEN(ObjOpenDevice) with the ObjDeviceKbd family and the explicit
 * exclusive-claim option claims the controller, OBJINVOKE(Read) returns raw
 * scan-code bytes and OBJINVOKE(ObjOpKbdLeds) sets LED state through a
 * Vcontrol Handle (handle 0 is rejected).  ObjOpenKbd and the historical
 * KBDOPEN/KBDREAD/KBDCLOSE/KBDLEDS numbers are reserved and dispatch to
 * nothing.
 */

#endif
