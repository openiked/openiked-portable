/*
 * Copyright (c) 2016 Reyk Floeter <reyk@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/socket.h>
#include <unistd.h>

#include "openbsd-compat.h"

#undef socket
#undef accept4

#if defined(SOCK_SETFLAGS)
#include <fcntl.h>

static int
bsd_socket_setflags(int s, int flags)
{
#ifdef _WIN32
	/* see libressl/tests/compat/pipe2.c */
	if (flags & FD_CLOEXEC) {
		HANDLE h = (HANDLE)_get_osfhandle(s);
		if (h != NULL)
			if (SetHandleInformation(h, HANDLE_FLAG_INHERIT, 0) == 0)
				return (-1);
	}
	if (flags & O_NONBLOCK) {
		unsigned long mode = 1;
		if (ioctlsocket(s, FIONBIO, &mode) != 0)
			return (-1);
	}
	return (0);
#else
	int	 f;

	if (flags & SOCK_NONBLOCK) {
		if (fcntl(s, F_GETFL, &f) == -1)
			return (-1);
		f |= O_NONBLOCK;
		if (fcntl(s, F_SETFL, &f) == -1)
			return (-1);
	}

	if (flags & SOCK_CLOEXEC) {
		if (fcntl(s, F_GETFD, &f) == -1)
			return (-1);
		f |= FD_CLOEXEC;
		if (fcntl(s, F_SETFD, &f) == -1)
			return (-1);
	}

	return (0);
#endif
}
#endif

int
bsd_socket(int domain, int type, int protocol)
{
	int	 s;
#if defined(SOCK_SETFLAGS)
	int	 setfl;

	setfl = type & SOCK_SETFLAGS;
	type &= ~SOCK_SETFLAGS;
#endif

	if ((s = socket(domain, type, protocol)) == -1)
		return (-1);

#if defined(SOCK_SETFLAGS)
	if (bsd_socket_setflags(s, setfl) == -1) {
		close(s);
		return (-1);
	}
#endif

	return (s);
}

#if 0
/* conflicts w/ libressl/compat/pipe2.c */
int
bsd_socketpair(int d, int type, int protocol, int sv[2])
{
#if defined(SOCK_SETFLAGS)
	int	 setfl;
	int	 i;

	setfl = type & SOCK_SETFLAGS;
	type &= ~SOCK_SETFLAGS;
#endif

	if (socketpair(d, type, protocol, sv) == -1)
		return (-1);

#if defined(SOCK_SETFLAGS)
	for (i = 0; i < 2; i++) {
		if (bsd_socket_setflags(sv[i], setfl) == -1) {
			close(sv[0]);
			close(sv[1]);
			return (-1);
		}
	}
#endif

	return (0);
}
#endif

int
bsd_accept4(int s, struct sockaddr *addr, socklen_t *addrlen, int flags)
{
#if !defined(SOCK_SETFLAGS) && defined(HAVE_ACCEPT4)
	return (accept4(s, addr, addrlen, flags));
#elif defined(SOCK_SETFLAGS)
	int	 c, setfl;

	setfl = flags & SOCK_SETFLAGS;
	flags &= ~SOCK_SETFLAGS;
	if ((c = accept(s, addr, addrlen)) == -1)
		return (-1);
	if (bsd_socket_setflags(c, setfl) == -1) {
		close(c);
		return (-1);
	}
	return (c);
#elif defined(__NetBSD__)
	return (paccept(s, addr, addrlen, NULL, flags));
#endif
}
