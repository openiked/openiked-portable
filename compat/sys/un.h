/*
 * Public domain
 * sys/types.h compatibility shim
 */

#ifndef _MSC_VER
#include_next <sys/un.h>
#else

#ifndef IKED_COMPAT_SYS_UN_H
#define IKED_COMPAT_SYS_UN_H

#include <afunix.h>

#endif /* !IKED_COMPAT_SYS_UN_H */
#endif
