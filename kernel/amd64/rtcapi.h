#ifndef PLAN2001_RTCAPI_H
#define PLAN2001_RTCAPI_H
typedef struct Rtcdate Rtcdate;
struct Rtcdate {
	int year;	/* 1970..2069, 4-digit */
	int month;	/* 1..12 */
	int day;	/* 1..31, validated for month/leap year */
	int hour;	/* 0..23 */
	int minute;	/* 0..59 */
	int second;	/* 0..59 */
};
int rtcnativeget(Rtcdate *d);	/* 0 ok, -1 with errstr */
int rtcnativeset(Rtcdate *d);	/* 0 ok, -1 with errstr; requires authority */
#endif
