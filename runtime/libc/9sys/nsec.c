#include <u.h>
#include <libc.h>

/*
 * The direct _NSEC(53) call is the simple equivalent of the former
 * ProcessSelf OBJOPEN/OBJINVOKE(ObjOpTimeNow)/OBJCLOSE adapter: both read
 * the same 64-bit todget(nil,nil) source.  The direct call has no Handle,
 * no allocation and no failure path.
 */
extern vlong _nsec(void);

vlong
nsec(void)
{
	return _nsec();
}
