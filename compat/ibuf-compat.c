/*      $OpenBSD: imsg-buffer.c,v 1.16 2023/06/19 17:19:50 claudio Exp $        */

/*
 * Copyright (c) 2003, 2004 Henning Brauer <henning@openbsd.org>
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

/* ibuf API functions added to OpenBSD's imsg-buffer.c in June 2023. */

#ifdef HAVE_IMSG_H

#if !defined(HAVE_IBUF_ADD_BUF) || !defined(HAVE_IBUF_ADD_ZERO)
#include <sys/queue.h>

#include <string.h>
#include <imsg.h>
#endif /* !defined(HAVE_IBUF_ADD_BUF) || !defined(HAVE_IBUF_ADD_ZERO) */

#if !defined(HAVE_IBUF_ADD_BUF)
void *ibuf_reserve(struct ibuf *, size_t);

int
ibuf_add(struct ibuf *buf, const void *data, size_t len)
{
	void *b;

	if ((b = ibuf_reserve(buf, len)) == NULL)
		return (-1);

	memcpy(b, data, len);
	return (0);
}

int
ibuf_add_buf(struct ibuf *buf, const struct ibuf *from)
{
	return ibuf_add(buf, from->buf, from->wpos);
}
#endif /* !defined(HAVE_IBUF_ADD_BUF) */

#if !defined(HAVE_IBUF_ADD_ZERO)
void *ibuf_reserve(struct ibuf *, size_t);

int
ibuf_add_zero(struct ibuf *buf, size_t len)
{
	void *b;

	if ((b = ibuf_reserve(buf, len)) == NULL)
		return (-1);
	return (0);
}
#endif /* !defined(HAVE_IBUF_ADD_ZERO) */

#if !defined(HAVE_IBUF_DATA)
void *ibuf_seek(struct ibuf *, size_t, size_t);

void *
ibuf_data(struct ibuf *buf)
{
	return (ibuf_seek(buf, 0, 0));
}
#endif /* !defined(HAVE_IBUF_DATA) */

#endif /* HAVE_IMSG_H */
