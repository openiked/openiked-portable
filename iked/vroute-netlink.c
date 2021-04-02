/*	$OpenBSD$	*/

/*
 * Copyright (c) 2021 Tobias Heider <tobhe@openbsd.org>
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

#include <sys/ioctl.h>
#include <sys/socket.h>

#include <asm/types.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <netinet/in.h>

#include <linux/ipv6.h>

#include <event.h>
#include <string.h>

#include <iked.h>

int vroute_setroute(struct iked *, uint8_t, struct sockaddr *, uint8_t,
    struct sockaddr *, int);
int vroute_doroute(struct iked *, int, int, int, uint8_t, struct sockaddr *,
    struct sockaddr *, struct sockaddr *, int *);
int vroute_doaddr(struct iked *, int, struct sockaddr *, struct sockaddr *, int);

struct iked_vroute_sc {
	int	ivr_rtsock;
};

void
vroute_init(struct iked *env)
{
	struct iked_vroute_sc	*ivr;

	ivr = calloc(1, sizeof(*ivr));
	if (ivr == NULL)
		fatal("%s: calloc.", __func__);

	if ((ivr->ivr_rtsock = socket(AF_NETLINK, SOCK_DGRAM,
	    NETLINK_ROUTE)) == -1)
		fatal("%s: failed to create netlink socket", __func__);

	env->sc_vroute = ivr;
}

int
vroute_getaddr(struct iked *env, struct imsg *imsg)
{
	struct sockaddr		*addr, *mask;
	uint8_t			*ptr;
	size_t			 left;
	int			 af;
	unsigned int		 ifidx;

	ptr = imsg->data;
	left = IMSG_DATA_SIZE(imsg);

	if (left < sizeof(*addr))
		fatalx("bad length imsg received");

	addr = (struct sockaddr *) ptr;
	af = addr->sa_family;

	if (left < SA_LEN(addr))
		fatalx("bad length imsg received");
	ptr += SA_LEN(addr);
	left -= SA_LEN(addr);

	if (left < sizeof(*mask))
		fatalx("bad length imsg received");
	mask = (struct sockaddr *) ptr;
	if (mask->sa_family != af)
		return (-1);

	if (left < SA_LEN(mask))
		fatalx("bad length imsg received");
	ptr += SA_LEN(mask);
	left -= SA_LEN(mask);

	if (left != sizeof(ifidx))
		fatalx("bad length imsg received");
	memcpy(&ifidx, ptr, sizeof(ifidx));
	ptr += sizeof(ifidx);
	left -= sizeof(ifidx);

	return (vroute_doaddr(env, ifidx, addr, mask,
	    imsg->hdr.type == IMSG_IF_ADDADDR));
}

int
vroute_setaddroute(struct iked *env, uint8_t rdomain, struct sockaddr *dst,
    uint8_t mask, struct sockaddr *ifa)
{
	return (vroute_setroute(env, rdomain, dst, mask, ifa,
	    IMSG_VROUTE_ADD));
}

int
vroute_setcloneroute(struct iked *env, uint8_t rdomain, struct sockaddr *dst,
    uint8_t mask, struct sockaddr *addr)
{
	return (0);
}

int
vroute_setdelroute(struct iked *env, uint8_t rdomain, struct sockaddr *dst,
    uint8_t mask, struct sockaddr *addr)
{
	return (0);
}

int
vroute_setroute(struct iked *env, uint8_t rdomain, struct sockaddr *dst,
    uint8_t mask, struct sockaddr *addr, int type)
{
	return (0);
}

int
vroute_getroute(struct iked *env, struct imsg *imsg)
{
	return (0);
}

int
vroute_getcloneroute(struct iked *env, struct imsg *imsg)
{
	return (0);
}

int
vroute_doroute(struct iked *env, int flags, int addrs, int rdomain, uint8_t type,
    struct sockaddr *dest, struct sockaddr *mask, struct sockaddr *addr, int *need_gw)
{
	return (0);
}

int
vroute_doaddr(struct iked *env, int ifidx, struct sockaddr *addr,
    struct sockaddr *mask, int add)
{
	struct iked_vroute_sc	*ivr = env->sc_vroute;
	struct sockaddr_in	*in;
	struct sockaddr_in6	*in6;
	int			 af;
	char			 addr_buf[NI_MAXHOST];
	char			 mask_buf[NI_MAXHOST];
	struct {
		struct nlmsghdr		hdr;
		struct ifaddrmsg	ifa;
		char			buf[256];
	} req;
	struct rtattr		*rta;

	af = addr->sa_family;
	req.hdr.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifaddrmsg));
	req.hdr.nlmsg_type = add ? RTM_NEWADDR : RTM_DELADDR;
	req.hdr.nlmsg_flags = NLM_F_REQUEST;
	if (add)
		req.hdr.nlmsg_flags |= NLM_F_CREATE | NLM_F_EXCL;
	req.ifa.ifa_family = af;
	req.ifa.ifa_scope = RT_SCOPE_UNIVERSE;
	req.ifa.ifa_index = ifidx;

	/* Add attribute */
	if (NLMSG_ALIGN(req.hdr.nlmsg_len) + RTA_LENGTH(SA_LEN(addr))
	    > sizeof(req.buf)) {
		log_info("%s: buffer too small.", __func__);
		return (0);
	}
	rta = (struct rtattr *)(((char *)&req.hdr) +
	    NLMSG_ALIGN(req.hdr.nlmsg_len));
	rta->rta_type = IFA_LOCAL;
	switch(af) {
	case AF_INET:
		inet_ntop(af, &((struct sockaddr_in *)addr)->sin_addr,
		    addr_buf, sizeof(addr_buf));
		inet_ntop(af, &((struct sockaddr_in *)mask)->sin_addr,
		    mask_buf, sizeof(mask_buf));
		log_debug("%s: %s inet %s netmask %s", __func__,
		    add ? "add" : "del",addr_buf, mask_buf);

		req.ifa.ifa_prefixlen = mask2prefixlen(mask);
		in = (struct sockaddr_in *) addr;
		memcpy(RTA_DATA(rta), &in->sin_addr.s_addr, 4);
		rta->rta_len = RTA_LENGTH(4);
		break;
	case AF_INET6:
		inet_ntop(af, &((struct sockaddr_in6 *)addr)->sin6_addr,
		    addr_buf, sizeof(addr_buf));
		inet_ntop(af, &((struct sockaddr_in6 *)mask)->sin6_addr,
		    mask_buf, sizeof(mask_buf));
		log_debug("%s: %s inet6 %s netmask %s", __func__,
		    add ? "add" : "del",addr_buf, mask_buf);

		req.ifa.ifa_prefixlen = mask2prefixlen6(mask);
		in6 = (struct sockaddr_in6 *) addr;
		memcpy(RTA_DATA(rta), &in6->sin6_addr.s6_addr, 16);
		rta->rta_len = RTA_LENGTH(16);
		break;
	}
	req.hdr.nlmsg_len = NLMSG_ALIGN(req.hdr.nlmsg_len) + RTA_ALIGN(rta->rta_len);

	if (send(ivr->ivr_rtsock, &req, req.hdr.nlmsg_len, 0) == -1)
		log_warn("%s: send", __func__);

	return (0);
}
