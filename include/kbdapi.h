#ifndef _KBDAPI_H_
#define _KBDAPI_H_

/*
 * PS/2 keyboard access is reached only through the five-operation OBJ ABI:
 * OBJOPEN(ObjOpenKbd) claims the controller, OBJINVOKE(Read) returns raw
 * scan-code bytes and OBJINVOKE(ObjOpKbdLeds) sets the eve-only LED state
 * (handle 0 is the global LED control that does not claim the controller).
 * The historical KBDOPEN/KBDREAD/KBDCLOSE/KBDLEDS numbers are reserved and
 * dispatch to nothing.
 */

#endif
