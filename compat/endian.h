/*
 * Public domain
 * endian.h compatibility shim
 */

#ifndef IKED_COMPAT_ENDIAN_H
#define IKED_COMPAT_ENDIAN_H

#ifdef HAVE_ENDIAN_H
#include_next <endian.h>
#endif /* HAVE_ENDIAN_H */

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
#define be16toh betoh16
#define be32toh betoh32
#define be64toh betoh64
#endif /* __APPLE__ && !HAVE_ENDIAN_H */

#if defined(_WIN32) && !defined(HAVE_ENDIAN_H)
#include <winsock2.h>
#define betoh16(x) ntohs((x))
#define htobe16(x) htons((x))
#define betoh32(x) ntohl((x))
#define htobe32(x) ntohl((x))
#define betoh64(x) ntohll((x))
#define htobe64(x) ntohll((x))
#define be16toh betoh16
#define be32toh betoh32
#define be64toh betoh64
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

#if defined(__FreeBSD__)
#if !defined(HAVE_ENDIAN_H)
#include <sys/endian.h>
#endif
#if !defined(betoh16)
#define betoh16	be16toh
#endif
#if !defined(betoh32)
#define betoh32	be32toh
#endif
#if !defined(betoh64)
#define betoh64	be64toh
#endif
#endif

#if defined(__NetBSD__)
#if !defined(betoh16)
#define betoh16	be16toh
#endif
#if !defined(betoh32)
#define betoh32	be32toh
#endif
#if !defined(betoh64)
#define betoh64	be64toh
#endif
#endif

#endif /* IKED_COMPAT_ENDIAN_H */
