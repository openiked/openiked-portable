/* Placed in the public domain */

#include "openbsd-compat.h"

#if !defined(HAVE_GETRTABLE)
int
getrtable(void)
{
	return (0);
}
#endif

#if !defined(HAVE_SETRTABLE)
int
setrtable(int rtableid)
{
	if (rtableid == 0)
		return (0);
	return (-1);
}
#endif
