typedef struct Ledport Ledport;

struct Ledport {
	uchar	nled;
	uchar	led;
	ushort	ledbits;		/* implementation dependent */
};

/* http://en.wikipedia.org/wiki/IBPI */
enum {
	Ibpinone,
	Ibpinormal,
	Ibpilocate,
	Ibpifail,
	Ibpirebuild,
	Ibpipfa,
	Ibpispare,
	Ibpicritarray,
	Ibpifailarray,
	Ibpilast,
};

char	*ledname(int);
int	name2led(char*);
