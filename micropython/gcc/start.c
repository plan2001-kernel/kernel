#include <u.h>
#include <libc.h>

int main(int, char **);
extern char *argv0;

void **_privates;
int _nprivates;
static void *privates[16];

void
p9_start(int argc, char **argv)
{
	_privates = privates;
	_nprivates = 16;
	if(argc > 0)
		argv0 = argv[0];
	int r = main(argc, argv);
	exits(r == 0 ? "" : "error");
}
