/*	$OpenBSD: $	*/

/*
 * Copyright (c) 2020-2021 Tobias Heider <tobhe@openbsd.org>
 * Copyright (c) 2020 Markus Friedl
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

#include <sys/queue.h>
#include <sys/socket.h>
#include <event.h>

#include "iked.h"

int
ipsec_couple(struct iked *env, struct iked_sas *sas, int couple)
{
	return pfkey_couple(env, sas, couple);
}

int
ipsec_sa_last_used(struct iked *env, struct iked_childsa *sa, uint64_t *last_used)
{
	return pfkey_sa_last_used(env, sa, last_used);
}

int
ipsec_flow_add(struct iked *env, struct iked_flow *flow)
{
	return pfkey_flow_add(env, flow);
}

int
ipsec_flow_delete(struct iked *env, struct iked_flow *flow)
{
	return pfkey_flow_delete(env, flow);
}

int
ipsec_sa_init(struct iked *env, struct iked_childsa *sa, uint32_t *spi)
{
	return pfkey_sa_init(env, sa, spi);
}

int
ipsec_sa_add(struct iked *env, struct iked_childsa *sa, struct iked_childsa *last)
{
	return pfkey_sa_add(env, sa, last);
}

int
ipsec_sa_update_addresses(struct iked *env, struct iked_childsa *sa)
{
	return pfkey_sa_update_addresses(env, sa);
}

int
ipsec_sa_delete(struct iked *env, struct iked_childsa *sa)
{
	return pfkey_sa_delete(env, sa);
}

int
ipsec_socket(struct iked *env)
{
	return pfkey_socket(env);
}

void
ipsec_init(struct iked *env, int fd)
{
	pfkey_init(env, fd);
}
