#include <u.h>
#include <libc.h>
#include "../../abi/obj.h"

void (*__assert)(char*);

void
_assert(char *s)
{
	if(__assert)
		(*__assert)(s);
    {
        ObjOpenConsoleReq req = {2, 0};
        char buf[256];
        int h, n;

        n = snprint(buf, sizeof buf, "assert failed: %s\n", s);
        h = obj_open(ObjOpenConsole, &req, sizeof req, 0, nil);
        if(h >= 0){
            obj_write(h, buf, n, 0);
            obj_close(h);
        }
    }
	abort();
}
