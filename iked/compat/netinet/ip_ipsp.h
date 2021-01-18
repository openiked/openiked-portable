/*
 * Copyright (c) 2021 Tobias Heider <tobh@openbsd.org>
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

#ifdef HAVE_IPSP_H
#include_next "netinet/ip_ipsp.h"
#else

#ifdef HAVE_NET_IPSEC_H
#include <netinet/in.h>
#include <sys/types.h>
#include <netipsec/ipsec.h>
#endif
#ifdef HAVE_LINUX_IPSEC_H
#include <linux/ipsec.h>
#endif
#ifdef HAVE_NETINET6_IPSEC_H
#include <netinet6/ipsec.h>
#endif

#if !defined HAVE_IPSP_H && (defined HAVE_NET_IPSEC_H || \
    defined HAVE_LINUX_IPSEC_H || defined HAVE_NETINET6_IPSEC_H)
#if !defined(IPSP_DIRECTION_IN)
#define IPSP_DIRECTION_IN	IPSEC_DIR_INBOUND
#endif
#if !defined(IPSP_DIRECTION_OUT)
#define IPSP_DIRECTION_OUT	IPSEC_DIR_OUTBOUND
#endif
#endif

#endif /* HAVE_IPSP_H */
