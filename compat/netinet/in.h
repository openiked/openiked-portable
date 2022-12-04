/*
 * Public domain
 * in.h compatibility shim
 */

#ifdef _MSC_VER
#elif defined(__linux__)
#ifndef IKED_COMPAT_NETINET_IN_H
#define IKED_COMPAT_NETINET_IN_H
#include_next <netinet/in.h>
#define IPPROTO_IPV4	IPPROTO_IPIP
#endif
#else /* OpenBSD */
#include_next <netinet/in.h>
#endif
