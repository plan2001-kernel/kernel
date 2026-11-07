#ifndef _RTCAPI_H_
#define _RTCAPI_H_

typedef struct Rtcdate Rtcdate;
struct Rtcdate
{
	int	year;		/* 1970..2069 */
	int	month;		/* 1..12 */
	int	day;		/* 1..31, validated */
	int	hour;		/* 0..23 */
	int	minute;		/* 0..59 */
	int	second;		/* 0..59 */
};

long	rtcget(Rtcdate*);
long	rtcset(Rtcdate*);

#endif
