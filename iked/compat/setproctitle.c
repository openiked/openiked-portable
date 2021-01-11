/* Placed in the public domain */

#include "openbsd-compat.h"

#ifndef HAVE_SETPROCTITLE
void
setproctitle(const char *fmt, ...)
{
}
#endif /* HAVE_SETPROCTITLE */
