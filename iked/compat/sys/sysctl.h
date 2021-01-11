/*
 * Public domain
 * sys/sysctl.h compatibility shim
 */

#if defined _MSC_VER

#ifndef IKED_COMPAT_SYS_SYSCTL_H
#define IKED_COMPAT_SYS_SYSCTL_H

/* XXX */

#endif /* !IKED_COMPAT_SYS_SYSCTL_H */

#elif defined __linux__
#include <linux/sysctl.h>
#else
#include_next <sys/sysctl.h>
#endif
