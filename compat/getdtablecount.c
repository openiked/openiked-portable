/* Placed in the public domain */

#include "openbsd-compat.h"

#if !defined(HAVE_GETDTABLECOUNT)
int
getdtablecount(void)
{
	return (0);
}
#endif
