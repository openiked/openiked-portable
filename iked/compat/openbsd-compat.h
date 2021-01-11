/* Placed in the public domain */

#ifndef _OPENBSD_COMPAT_H
#define _OPENBSD_COMPAT_H

#define YYSTYPE_IS_DECLARED 1	/* for bison */

#ifndef LOGIN_NAME_MAX
# define LOGIN_NAME_MAX 9
#endif

#if defined(HAVE_ENDIAN_H)
#include <endian.h>
#endif

#if defined(__APPLE__) && !defined(HAVE_ENDIAN_H)
#include <libkern/OSByteOrder.h>
#define betoh16(x) OSSwapBigToHostInt16((x))
#define htobe16(x) OSSwapHostToBigInt16((x))
#define betoh32(x) OSSwapBigToHostInt32((x))
#define htobe32(x) OSSwapHostToBigInt32(x)
#define htole64(x) OSSwapHostToLittleInt64(x)
#define htobe64(x) OSSwapHostToBigInt64(x)
#define letoh64(x) OSSwapLittleToHostInt64(x)
#define betoh64(x) OSSwapBigToHostInt64(x)
#endif /* __APPLE__ && !HAVE_ENDIAN_H */

#if defined(_WIN32) && !defined(HAVE_ENDIAN_H)
#if !defined(_MSC_VER)
#include <sys/param.h>
#endif
#include <winsock2.h>
#define betoh16(x) ntohs((x))
#define htobe16(x) htons((x))
#define betoh32(x) ntohl((x))
#define htobe32(x) ntohl((x))
#define betoh64(x) ntohll((x))
#define htobe64(x) ntohll((x))
#endif /* _WIN32 && !HAVE_ENDIAN_H */

#ifdef __linux__
#if !defined(betoh16)
#define betoh16	be16toh
#endif
#if !defined(betoh32)
#define betoh32	be32toh
#endif
#if !defined(betoh64)
#define betoh64	be64toh
#endif
#endif /* __linux__ */

#if defined(__FreeBSD__) && !defined(HAVE_ENDIAN_H)
#include <sys/endian.h>
#endif

#include <stdlib.h>

#if !defined(HAVE_STRLCAT)
size_t strlcat(char *, const char *, size_t);
#endif

#if !defined(HAVE_STRLCPY)
size_t strlcpy(char *, const char *, size_t);
#endif

#if !defined(HAVE_REALLOCARRAY)
void *reallocarray(void *, size_t, size_t);
#endif

#if !defined(HAVE_RECALLOCARRAY)
void *recallocarray(void *, size_t, size_t, size_t);
#endif

#if !defined(HAVE_EXPLICIT_BZERO)
void explicit_bzero(void *, size_t);
#endif

#if !defined(HAVE_GETPAGESIZE)
int getpagesize(void);
#endif

#if !defined(HAVE_TIMINGSAFE_BCMP)
int timingsafe_bcmp(const void *, const void *, size_t);
#endif

#if !defined(HAVE_ACCEPT4)
#include <sys/socket.h>
#define accept4 bsd_accept4
int bsd_accept4(int, struct sockaddr *, socklen_t *, int flags);
#endif

#if !defined(HAVE_SOCK_NONBLOCK)
#define SOCK_NONBLOCK	0x4000	/* Set O_NONBLOCK */
#define SOCK_CLOEXEC	0x8000	/* Set FD_CLOEXEC */
#define SOCK_SETFLAGS	0xf000	/* Set flags as checked above */
#define socket bsd_socket
int bsd_socket(int domain, int type, int protocol);
#endif

#if !defined(HAVE_SETPROCTITLE)
void setproctitle(const char *fmt, ...);
#endif

#if !defined(HAVE_SETRESGID)
int setresgid(gid_t rgid, gid_t egid, gid_t sgid);
#endif

#if !defined(HAVE_SETRESUID)
int setresuid(uid_t ruid, uid_t euid, uid_t suid);
#endif

#ifdef _WIN32
#include <direct.h>
uid_t geteuid(void);
#endif

#if !defined(HAVE_GETRTABLE)
int getrtable(void);
#endif

#if !defined(HAVE_SETRTABLE)
int setrtable(int rtableid);
#endif

#if !defined(HAVE_STRTONUM)
long long
strtonum(const char *nptr, long long minval, long long maxval,
    const char **errstr);
#endif

#if !defined(HAVE_FREEZERO)
void freezero(void *ptr, size_t size);
#endif

#if !defined(HAVE_GETDTABLECOUNT)
int getdtablecount(void);
#endif

#if !defined(HAVE_GETOPT)
#include "getopt.h"
#endif

#if !defined(HAVE_USLEEP)
int usleep(unsigned int x);
#endif

#ifdef HAVE_SOCKADDR_SA_LEN
#ifndef SA_LEN
#define SA_LEN(sa)      (sa)->sa_len
#endif
#ifndef SS_LEN
#define SS_LEN(ss)      (ss).ss_len
#endif
#else
#define SA_LEN(sa)      						\
	((sa->sa_family == AF_INET)  ? sizeof(struct sockaddr_in) :	\
	(sa->sa_family == AF_INET6) ? sizeof(struct sockaddr_in6) :	\
	sizeof(struct sockaddr))
#define SS_LEN(ss)							\
	((ss.ss_family == AF_INET)  ? sizeof(struct sockaddr_in) :	\
	(ss.ss_family == AF_INET6) ? sizeof(struct sockaddr_in6) :	\
	sizeof(struct sockaddr_storage))
#endif

#ifndef HAVE_FFS
int ffs(int);
#endif

#ifdef __OpenBSD__
typedef int evutil_socket_t;
#endif

#endif /* !_OPENBSD_COMPAT_H */
