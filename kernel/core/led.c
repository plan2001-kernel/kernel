#include "u.h"
#include "../core/lib.h"
#include "mem.h"
#include "dat.h"
#include "../core/error.h"
#include "fns.h"
#include "led.h"

static char *ibpinames[Ibpilast] = {
[Ibpinone]	"none",
[Ibpinormal]	"normal",
[Ibpilocate]	"locate",
[Ibpifail]		"fail",
[Ibpirebuild]	"rebuild",
[Ibpipfa]		"pfa",
[Ibpispare]	"spare",
[Ibpicritarray]	"critarray",
[Ibpifailarray]	"failarray",
};

char*
ledname(int c)
{
	if(c >= 0 && c < Ibpilast)
		return ibpinames[c];
	return "bad index";
}

 int
name2led(char *s)
{
	int i;

	for(i = 0; i < nelem(ibpinames); i++)
		if(strcmp(ibpinames[i], s) == 0)
			return i;
	return -1;
}
