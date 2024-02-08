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
#include <unistd.h>

#ifdef WITH_SYSTEMD
#include "systemd/sd-bus.h"
#endif

#include "iked.h"

int vroute_setroute(struct iked *, uint32_t, struct sockaddr *, uint8_t,
    struct sockaddr *, int);
int vroute_doroute(struct iked *, uint8_t, struct sockaddr *,
    struct sockaddr *, struct sockaddr *, int);
int vroute_doaddr(struct iked *, int, struct sockaddr *, struct sockaddr *, int);
int vroute_dodns(struct iked *, int, unsigned int);
void vroute_cleanup(struct iked *);

void vroute_insertaddr(struct iked *, int, struct sockaddr *, struct sockaddr *);
void vroute_removeaddr(struct iked *, int, struct sockaddr *, struct sockaddr *);
void vroute_insertdns(struct iked *, int, struct sockaddr *);
void vroute_removedns(struct iked *, int, struct sockaddr *);
void vroute_insertroute(struct iked *, struct sockaddr *);
void vroute_removeroute(struct iked *, struct sockaddr *);
#ifdef WITH_SYSTEMD
int vroute_dbus_default_route(struct iked *, int, sd_bus_error *, int,
    const char *, const char *, const char *);
int vroute_dbus_dns(struct iked *, int, sd_bus_error *, int, const char *,
    const char *, const char *);
#endif

struct vroute_addr {
	int				va_ifidx;
	struct	sockaddr_storage	va_addr;
	struct	sockaddr_storage	va_mask;
	TAILQ_ENTRY(vroute_addr)	va_entry;
};
TAILQ_HEAD(vroute_addrs, vroute_addr);

struct vroute_route {
	struct	sockaddr_storage	vr_dest;
	TAILQ_ENTRY(vroute_route)	vr_entry;
};
TAILQ_HEAD(vroute_routes, vroute_route);

struct vroute_dns {
	struct	sockaddr_storage	vd_addr;
	int				vd_ifidx;
	TAILQ_ENTRY(vroute_dns)		vd_entry;
};
TAILQ_HEAD(vroute_dnss, vroute_dns);

static int	nl_addattr(struct nlmsghdr *, int, void *, size_t);
static int	nl_dorule(struct iked *, int, uint32_t, int, int);

struct iked_vroute_sc {
	struct vroute_addrs	 ivr_addrs;
	struct vroute_dnss	 ivr_dnss;
	struct vroute_routes	 ivr_routes;
	int			 ivr_rtsock;
#ifdef WITH_SYSTEMD
	sd_bus			*ivr_bus;
#endif
};

#define NL_BUFLEN	1024
#define IKED_RT_TABLE	210
#define IKED_RT_PRIO	210

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

	TAILQ_INIT(&ivr->ivr_addrs);
	TAILQ_INIT(&ivr->ivr_dnss);
	TAILQ_INIT(&ivr->ivr_routes);

	env->sc_vroute = ivr;
	nl_dorule(env, IKED_RT_TABLE, IKED_RT_PRIO, AF_INET, 1);
	nl_dorule(env, IKED_RT_TABLE, IKED_RT_PRIO, AF_INET6, 1);
}

void
vroute_cleanup(struct iked *env)
{
	struct iked_vroute_sc	*ivr = env->sc_vroute;
	struct vroute_addr	*addr;
	struct vroute_route	*route;
	struct vroute_dns	*dns;

	while ((addr = TAILQ_FIRST(&ivr->ivr_addrs))) {
		vroute_doaddr(env, addr->va_ifidx,
		    (struct sockaddr *)&addr->va_addr,
		    (struct sockaddr *)&addr->va_mask, 0);
		TAILQ_REMOVE(&ivr->ivr_addrs, addr, va_entry);
		free(addr);
	}

	while ((route = TAILQ_FIRST(&ivr->ivr_routes))) {
		vroute_doroute(env, RTM_DELROUTE,
		    (struct sockaddr *)&route->vr_dest, NULL, NULL, 1);
		TAILQ_REMOVE(&ivr->ivr_routes, route, vr_entry);
		free(route);
	}

	while ((dns = TAILQ_FIRST(&ivr->ivr_dnss))) {
		vroute_dodns(env, 0, dns->vd_ifidx);
		TAILQ_REMOVE(&ivr->ivr_dnss, dns, vd_entry);
		free(dns);
	}
}

int
vroute_setaddr(struct iked *env, int add, struct sockaddr *addr,
    int mask, unsigned int ifidx)
{
	struct iovec		 iov[4];
	int			 iovcnt;
	struct sockaddr_in	 mask4;
	struct sockaddr_in6	 mask6;

	iovcnt = 0;
	iov[0].iov_base = addr;
	iov[0].iov_len = SA_LEN(addr);
	iovcnt++;

	switch(addr->sa_family) {
	case AF_INET:
		bzero(&mask, sizeof(mask));
		mask4.sin_addr.s_addr = prefixlen2mask(mask ? mask : 32);
		mask4.sin_family = AF_INET;

		iov[1].iov_base = &mask4;
		iov[1].iov_len = sizeof(mask4);
		iovcnt++;
		break;
	case AF_INET6:
		bzero(&mask6, sizeof(mask6));
		prefixlen2mask6(mask ? mask : 128,
		    (uint32_t *)&mask6.sin6_addr.s6_addr);
		mask6.sin6_family = AF_INET6;
		iov[1].iov_base = &mask6;
		iov[1].iov_len = sizeof(mask6);
		iovcnt++;
		break;
	default:
		return -1;
	}

	iov[2].iov_base = &ifidx;
	iov[2].iov_len = sizeof(ifidx);
	iovcnt++;

	return (proc_composev(&env->sc_ps, PROC_PARENT,
	    add ? IMSG_IF_ADDADDR : IMSG_IF_DELADDR, iov, iovcnt));
}

int
vroute_getaddr(struct iked *env, struct imsg *imsg)
{
	struct sockaddr		*addr, *mask;
	uint8_t			*ptr;
	size_t			 left;
	int			 af, add;
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

	add = (imsg->hdr.type == IMSG_IF_ADDADDR);
	/* Store address for cleanup */
	if (add)
		vroute_insertaddr(env, ifidx, addr, mask);
	else
		vroute_removeaddr(env, ifidx, addr, mask);

	return (vroute_doaddr(env, ifidx, addr, mask,
	    imsg->hdr.type == IMSG_IF_ADDADDR));
}

int
vroute_setdns(struct iked *env, int add, struct sockaddr *addr,
    unsigned int ifidx)
{
	struct iovec		 iov[2];

	if (addr == NULL)
		return (0);

	iov[0].iov_base = addr;
	iov[0].iov_len = SA_LEN(addr);

	iov[1].iov_base = &ifidx;
	iov[1].iov_len = sizeof(ifidx);

	return (proc_composev(&env->sc_ps, PROC_PARENT,
	    add ? IMSG_VDNS_ADD: IMSG_VDNS_DEL, iov, 2));
}

int
vroute_getdns(struct iked *env, struct imsg *imsg)
{
	struct sockaddr		*dns;
	uint8_t			*ptr;
	size_t			 left;
	int			 add;
	unsigned int		 ifidx;

	ptr = imsg->data;
	left = IMSG_DATA_SIZE(imsg);

	if (left < sizeof(*dns))
		fatalx("bad length imsg received");

	dns = (struct sockaddr *) ptr;
	if (left < SA_LEN(dns))
		fatalx("bad length imsg received");
	ptr += SA_LEN(dns);
	left -= SA_LEN(dns);

	if (left != sizeof(ifidx))
		fatalx("bad length imsg received");
	memcpy(&ifidx, ptr, sizeof(ifidx));
	ptr += sizeof(ifidx);
	left -= sizeof(ifidx);

	add = (imsg->hdr.type == IMSG_VDNS_ADD);
	if (add) {
		vroute_insertdns(env, ifidx, dns);
	} else {
		vroute_removedns(env, ifidx, dns);
	}

	return (vroute_dodns(env, add, ifidx));
}

void
vroute_insertroute(struct iked *env, struct sockaddr *dest)
{
	struct iked_vroute_sc	*ivr = env->sc_vroute;
	struct vroute_route	*route;

	route = calloc(1, sizeof(*route));
	if (route == NULL)
		fatalx("%s: calloc.", __func__);

	memcpy(&route->vr_dest, dest, SA_LEN(dest));

	TAILQ_INSERT_TAIL(&ivr->ivr_routes, route, vr_entry);
}

void
vroute_removeroute(struct iked *env, struct sockaddr *dest)
{
	struct iked_vroute_sc	*ivr = env->sc_vroute;
	struct vroute_route	*route, *troute;

	TAILQ_FOREACH_SAFE(route, &ivr->ivr_routes, vr_entry, troute) {
		if (sockaddr_cmp(dest, (struct sockaddr *)&route->vr_dest, -1))
			continue;
		TAILQ_REMOVE(&ivr->ivr_routes, route, vr_entry);
		free(route);
	}
}

void
vroute_insertdns(struct iked *env, int ifidx, struct sockaddr *addr)
{
	struct iked_vroute_sc	*ivr = env->sc_vroute;
	struct vroute_dns	*dns;

	dns = calloc(1, sizeof(*dns));
	if (dns == NULL)
		fatalx("%s: calloc.", __func__);

	memcpy(&dns->vd_addr, addr, SA_LEN(addr));
	dns->vd_ifidx = ifidx;

	TAILQ_INSERT_TAIL(&ivr->ivr_dnss, dns, vd_entry);
}

void
vroute_removedns(struct iked *env, int ifidx, struct sockaddr *addr)
{
	struct iked_vroute_sc	*ivr = env->sc_vroute;
	struct vroute_dns	*dns, *tdns;

	TAILQ_FOREACH_SAFE(dns, &ivr->ivr_dnss, vd_entry, tdns) {
		if (ifidx != dns->vd_ifidx)
			continue;
		if (sockaddr_cmp(addr, (struct sockaddr *) &dns->vd_addr, -1))
			continue;

		TAILQ_REMOVE(&ivr->ivr_dnss, dns, vd_entry);
		free(dns);
	}
}

void
vroute_insertaddr(struct iked *env, int ifidx, struct sockaddr *addr,
    struct sockaddr *mask)
{
	struct iked_vroute_sc	*ivr = env->sc_vroute;
	struct vroute_addr	*vaddr;

	vaddr = calloc(1, sizeof(*vaddr));
	if (vaddr == NULL)
		fatalx("%s: calloc.", __func__);

	memcpy(&vaddr->va_addr, addr, SA_LEN(addr));
	memcpy(&vaddr->va_mask, mask, SA_LEN(mask));
	vaddr->va_ifidx = ifidx;

	TAILQ_INSERT_TAIL(&ivr->ivr_addrs, vaddr, va_entry);
}

void
vroute_removeaddr(struct iked *env, int ifidx, struct sockaddr *addr,
    struct sockaddr *mask)
{
	struct iked_vroute_sc	*ivr = env->sc_vroute;
	struct vroute_addr	*vaddr, *tvaddr;

	TAILQ_FOREACH_SAFE(vaddr, &ivr->ivr_addrs, va_entry, tvaddr) {
		if (sockaddr_cmp(addr, (struct sockaddr *)&vaddr->va_addr, -1))
			continue;
		if (sockaddr_cmp(mask, (struct sockaddr *)&vaddr->va_mask, -1))
			continue;
		if (ifidx != vaddr->va_ifidx)
			continue;
		TAILQ_REMOVE(&ivr->ivr_addrs, vaddr, va_entry);
		free(vaddr);
	}
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
	return (vroute_setroute(env, rdomain, dst, mask, addr,
	    IMSG_VROUTE_CLONE));
}

int
vroute_setdelroute(struct iked *env, uint8_t rdomain, struct sockaddr *dst,
    uint8_t mask, struct sockaddr *addr)
{
	return (vroute_setroute(env, rdomain, dst, mask, addr,
	    IMSG_VROUTE_DEL));
}

int
vroute_setroute(struct iked *env, uint32_t rdomain, struct sockaddr *dst,
    uint8_t mask, struct sockaddr *addr, int type)
{
	struct sockaddr_storage	 sa;
	struct sockaddr_in	*in;
	struct sockaddr_in6	*in6;
	struct iovec		 iov[5];
	int			 iovcnt = 0;
	uint8_t			 af;

	if (addr && dst->sa_family != addr->sa_family)
		return (-1);
	af = dst->sa_family;

	iov[iovcnt].iov_base = &rdomain;
	iov[iovcnt].iov_len = sizeof(rdomain);
	iovcnt++;

	iov[iovcnt].iov_base = dst;
	iov[iovcnt].iov_len = SA_LEN(dst);
	iovcnt++;

	if (type != IMSG_VROUTE_CLONE && addr) {
		bzero(&sa, sizeof(sa));
		switch(af) {
		case AF_INET:
			in = (struct sockaddr_in *)&sa;
			in->sin_addr.s_addr = prefixlen2mask(mask);
			in->sin_family = af;
			iov[iovcnt].iov_base = in;
			iov[iovcnt].iov_len = sizeof(*in);
			iovcnt++;
			break;
		case AF_INET6:
			in6 = (struct sockaddr_in6 *)&sa;
			prefixlen2mask6(mask,
			    (uint32_t *)in6->sin6_addr.s6_addr);
			in6->sin6_family = af;
			iov[iovcnt].iov_base = in6;
			iov[iovcnt].iov_len = sizeof(*in6);
			iovcnt++;
			break;
		}

		iov[iovcnt].iov_base = addr;
		iov[iovcnt].iov_len = SA_LEN(addr);
		iovcnt++;
	}

	return (proc_composev(&env->sc_ps, PROC_PARENT, type, iov, iovcnt));
}

int
vroute_getroute(struct iked *env, struct imsg *imsg)
{
	struct sockaddr		*dest, *mask = NULL, *gateway = NULL;
	uint8_t			*ptr;
	size_t			 left;
	int			 type = 0;
	uint32_t		 rdomain;
	int			 host = 1;

	ptr = (uint8_t *)imsg->data;
	left = IMSG_DATA_SIZE(imsg);

	if (left < sizeof(rdomain))
		return (-1);
	rdomain = *ptr;
	ptr += sizeof(rdomain);
	left -= sizeof(rdomain);

	if (left < sizeof(struct sockaddr))
		return (-1);
	dest = (struct sockaddr *)ptr;

	if (left < SA_LEN(dest))
		return (-1);
	socket_setport(dest, 0);
	ptr += SA_LEN(dest);
	left -= SA_LEN(dest);

	if (left != 0) {
		if (left < sizeof(*mask))
			return (-1);
		mask = (struct sockaddr *)ptr;
		if (left < SA_LEN(mask))
			return (-1);
		socket_setport(mask, 0);
		ptr += SA_LEN(mask);
		left -= SA_LEN(mask);

		if (left < sizeof(*gateway))
			return (-1);
		gateway = (struct sockaddr *)ptr;
		if (left < SA_LEN(gateway))
			return (-1);
		socket_setport(gateway, 0);
		ptr += SA_LEN(gateway);
		left -= SA_LEN(gateway);

		/* Flow routes are sent with mask + gateway */
		host = 0;
	}

	switch(imsg->hdr.type) {
	case IMSG_VROUTE_ADD:
		type = RTM_NEWROUTE;
		break;
	case IMSG_VROUTE_DEL:
		vroute_removeroute(env, dest);
		type = RTM_DELROUTE;
		break;
	}

	return (vroute_doroute(env, type, dest, mask, gateway, host));
}

int
vroute_getcloneroute(struct iked *env, struct imsg *imsg)
{
	struct sockaddr		*dst;
	struct sockaddr_storage	 mask;
	uint8_t			*ptr;
	size_t			 left;
	uint32_t		 rdomain;

	ptr = (uint8_t *)imsg->data;
	left = IMSG_DATA_SIZE(imsg);

	if (left < sizeof(rdomain))
		return (-1);
	rdomain = *ptr;
	ptr += sizeof(rdomain);
	left -= sizeof(rdomain);

	bzero(&mask, sizeof(mask));

	if (left < sizeof(struct sockaddr))
		return (-1);
	dst = (struct sockaddr *)ptr;
	if (left < SA_LEN(dst))
		return (-1);
	ptr += SA_LEN(dst);
	left -= SA_LEN(dst);

	vroute_insertroute(env, dst);

	/*
	 * With rtnetlink(7) the host route is not cloned. Instead, we
	 * add a RTN_THROW route for the host in the IKED routing table.
	 */
	return (vroute_doroute(env, RTM_NEWROUTE, dst, NULL, NULL, 1));
}

int
vroute_doroute(struct iked *env, uint8_t type, struct sockaddr *dest,
    struct sockaddr *mask, struct sockaddr *addr, int host)
{
	char			 dest_buf[NI_MAXHOST];
	char			 addr_buf[NI_MAXHOST];
	struct iked_vroute_sc	*ivr = env->sc_vroute;
	struct sockaddr_in	*in;
	struct sockaddr_in6	*in6;
	int			 af;
	struct {
		struct nlmsghdr		hdr;
		char			buf[NL_BUFLEN];
	} req;
	struct rtmsg		*rtm;

	if (dest == NULL)
		return (-1);

	bzero(&req, sizeof(req));

	af = dest->sa_family;
	req.hdr.nlmsg_type = type;
	req.hdr.nlmsg_flags = NLM_F_REQUEST;
	if (type == RTM_NEWROUTE)
		req.hdr.nlmsg_flags |= NLM_F_CREATE | NLM_F_EXCL;
	req.hdr.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));

	rtm = NLMSG_DATA(&req.hdr);
	rtm->rtm_family = af;
	rtm->rtm_protocol = RTPROT_STATIC;
	if (host)
		rtm->rtm_type = RTN_THROW;
	else
		rtm->rtm_type = RTN_UNICAST;
	rtm->rtm_scope = RT_SCOPE_UNIVERSE;
	rtm->rtm_table = IKED_RT_TABLE;

	bzero(addr_buf, sizeof(addr_buf));

	switch (af) {
	case AF_INET:
		rtm->rtm_dst_len = mask ? mask2prefixlen(mask) : 32;

		in = (struct sockaddr_in *) dest;
		nl_addattr(&req.hdr, RTA_DST, &in->sin_addr.s_addr, 4);
		inet_ntop(af, &((struct sockaddr_in *)dest)->sin_addr,
		    dest_buf, sizeof(dest_buf));

		if (type == RTM_DELROUTE)
			goto send;

		if (addr) {
			inet_ntop(af, &((struct sockaddr_in *)addr)->sin_addr,
			    addr_buf, sizeof(addr_buf));
			in = (struct sockaddr_in *) addr;
			nl_addattr(&req.hdr, RTA_GATEWAY, &in->sin_addr.s_addr, 4);
		}
		break;
	case AF_INET6:
		rtm->rtm_dst_len = mask ? mask2prefixlen6(mask) : 128;

		in6 = (struct sockaddr_in6 *) dest;
		nl_addattr(&req.hdr, RTA_DST, &in6->sin6_addr.s6_addr, 16);
		inet_ntop(af, &((struct sockaddr_in6 *)dest)->sin6_addr,
		    dest_buf, sizeof(dest_buf));

		if (type == RTM_DELROUTE)
			goto send;

		if (addr) {
			inet_ntop(af, &((struct sockaddr_in6 *)addr)->sin6_addr,
			    addr_buf, sizeof(addr_buf));
			in6 = (struct sockaddr_in6 *) dest;
			nl_addattr(&req.hdr, RTA_GATEWAY,
			    &in6->sin6_addr.s6_addr, 16);
		}
		break;
	default:
		return (-1);
	}
send:
	log_debug("%s: len: %u type: %s dst %s/%d gateway %s flags %x", __func__, req.hdr.nlmsg_len,
	    type == RTM_NEWROUTE ? "RTM_NEWROUTE" : type == RTM_DELROUTE ? "RTM_DELROUTE" :
	    type == RTM_GETROUTE ? "RTM_GETROUTE" : "unknown", dest_buf, rtm->rtm_dst_len, addr_buf,
	    req.hdr.nlmsg_flags);

	if (send(ivr->ivr_rtsock, &req, req.hdr.nlmsg_len, 0) == -1)
		log_warn("%s: send", __func__);

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
		uint8_t			buf[NL_BUFLEN];
	} req;

	bzero(&req, sizeof(req));

	af = addr->sa_family;
	req.hdr.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifaddrmsg));
	req.hdr.nlmsg_type = add ? RTM_NEWADDR : RTM_DELADDR;
	req.hdr.nlmsg_flags = NLM_F_REQUEST;
	if (add)
		req.hdr.nlmsg_flags |= NLM_F_CREATE | NLM_F_EXCL;
	req.ifa.ifa_family = af;
	req.ifa.ifa_scope = RT_SCOPE_UNIVERSE;
	req.ifa.ifa_index = ifidx;

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
		nl_addattr(&req.hdr, IFA_LOCAL, &in->sin_addr.s_addr, 4);
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
		nl_addattr(&req.hdr, IFA_LOCAL, &in6->sin6_addr.s6_addr, 16);
		break;
	}

	if (send(ivr->ivr_rtsock, &req, req.hdr.nlmsg_len, 0) == -1)
		log_warn("%s: send", __func__);

	return (0);
}

int
vroute_dodns(struct iked *env, int add, unsigned int ifindex)
{
#ifdef WITH_SYSTEMD
	struct iked_vroute_sc *ivr = env->sc_vroute;
	const char *destination = "org.freedesktop.resolve1";
	const char *path = "/org/freedesktop/resolve1";
	const char *interface = "org.freedesktop.resolve1.Manager";
	sd_bus_error error = SD_BUS_ERROR_NULL;
	int ret;

	if (ivr->ivr_bus != NULL) {
		log_warnx("%s: vr_bus already set, internal error", __func__);
		return (0);
	}
	if (sd_bus_open_system(&ivr->ivr_bus) < 0) {
		log_warn("%s: sd_bus_open_system failed", __func__);
		ivr->ivr_bus = NULL;
		return (0);
	}

	ret = vroute_dbus_dns(env, ifindex, &error, add,
	    destination, path, interface);
	if (ret < 0 && sd_bus_error_has_name(&error,
	    "org.freedesktop.resolve1.LinkBusy")) {

		/*
                 * Interface is managed by networkd, so we need to send
                 * our messages there instead.
                 */
		destination = "org.freedesktop.network1";
		path = "/org/freedesktop/network1";
		interface = "org.freedesktop.network1.Manager";

		sd_bus_error_free(&error);
		ret = vroute_dbus_dns(env, ifindex, &error, add,
		    destination, path, interface);
	}
	if (ret < 0) {
		log_info("%s: vroute_dbus_dns: %s: %s", __func__,
		    error.name, error.message);
		sd_bus_error_free(&error);
	}
	ret = vroute_dbus_default_route(env, ifindex, &error, add,
	    destination, path, interface);
	if (ret < 0) {
		log_info("%s: vroute_dbus_default_route: %s: %s", __func__,
		    error.name, error.message);
		sd_bus_error_free(&error);
	}

	sd_bus_flush_close_unref(ivr->ivr_bus);
	ivr->ivr_bus = NULL;
#endif
	return (0);
}

static int
nl_dorule(struct iked *env, int table, uint32_t prio, int family, int add)
{
	struct iked_vroute_sc	*ivr = env->sc_vroute;
	struct {
		struct nlmsghdr		hdr;
		uint8_t			buf[NL_BUFLEN];
	} req;
	struct rtmsg		*rtm;

	bzero(&req, sizeof(req));

	req.hdr.nlmsg_flags = NLM_F_REQUEST;
	req.hdr.nlmsg_type = add ? RTM_NEWRULE : RTM_DELRULE;
	req.hdr.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));
	if (add)
		req.hdr.nlmsg_flags |= NLM_F_CREATE | NLM_F_EXCL;

	rtm = NLMSG_DATA(&req.hdr);
	rtm->rtm_family = family;
	rtm->rtm_protocol = RTPROT_BOOT;
	rtm->rtm_scope = RT_SCOPE_UNIVERSE;
	rtm->rtm_type = RTN_UNICAST;
	rtm->rtm_table = IKED_RT_TABLE;

	nl_addattr(&req.hdr, RTA_PRIORITY, &prio, sizeof(prio));

	if (send(ivr->ivr_rtsock, &req, req.hdr.nlmsg_len, 0) == -1)
		log_warn("%s: send", __func__);

	return (0);
}

static int
nl_addattr(struct nlmsghdr *hdr, int type, void *data, size_t len)
{
	struct rtattr	*rta;

	if ((NLMSG_ALIGN(hdr->nlmsg_len) + RTA_LENGTH(len)) > NL_BUFLEN) {
		log_info("%s: buffer too small.", __func__);
		return (-1);
	}

	rta = (struct rtattr *)(((char *)hdr) +
	    NLMSG_ALIGN(hdr->nlmsg_len));
	rta->rta_type = type;
	rta->rta_len = RTA_LENGTH(len);
	memcpy(RTA_DATA(rta), data, len);
	hdr->nlmsg_len = NLMSG_ALIGN(hdr->nlmsg_len) + RTA_ALIGN(rta->rta_len);
	return (0);
}

#ifdef WITH_SYSTEMD
int
vroute_dbus_default_route(struct iked *env, int ifidx, sd_bus_error *error, int add,
    const char *destination, const char *path, const char *interface)
{
	struct iked_vroute_sc *ivr = env->sc_vroute;
	sd_bus *bus = ivr->ivr_bus;
	int ret;

	ret = sd_bus_call_method(bus, destination, path, interface,
	    "SetLinkDefaultRoute", error, NULL, "ib", ifidx, add);
	return (ret);
}

int
vroute_dbus_dns(struct iked *env, int ifidx, sd_bus_error *error, int add,
    const char *destination, const char *path, const char *interface)
{
	struct sockaddr *dns;
	struct sockaddr_in *in;
	struct sockaddr_in6 *in6;
	struct iked_vroute_sc *ivr = env->sc_vroute;
	struct vroute_dns *vdns;
	sd_bus_message *req = NULL;
	sd_bus *bus = ivr->ivr_bus;
	int ret;

	ret = sd_bus_message_new_method_call(bus, &req,
	    destination, path, interface, "SetLinkDNS");
	if (ret < 0)
		return (-1);

	ret = sd_bus_message_append(req, "i", ifidx);
	if (ret < 0)
		return (-1);

	ret = sd_bus_message_open_container(req, 'a', "(iay)");
	if (ret < 0)
		return (-1);

	/* Empty list means remove all */
	if (add) {
		TAILQ_FOREACH(vdns, &ivr->ivr_dnss, vd_entry) {
			if (vdns->vd_ifidx != ifidx)
				continue;

			ret = sd_bus_message_open_container(req, 'r', "iay");
			if (ret < 0)
				return (-1);

			dns = (struct sockaddr *)&vdns->vd_addr;
			ret = sd_bus_message_append(req, "i", dns->sa_family);
			if (ret < 0)
				return (-1);

			switch (dns->sa_family) {
			case AF_INET:
				in = (struct sockaddr_in *)dns;
				ret = sd_bus_message_append_array(req, 'y',
				    &in->sin_addr, 4);
				if (ret < 0)
					return (-1);
				break;
			case AF_INET6:
				in6 = (struct sockaddr_in6 *)dns;
				ret = sd_bus_message_append_array(req, 'y',
				    &in6->sin6_addr, 16);
				if (ret < 0)
					return (-1);
				break;
			}
			ret = sd_bus_message_close_container(req);
			if (ret < 0)
				return (-1);
		}
	}

	ret = sd_bus_message_close_container(req);
	if (ret < 0)
		return (-1);

	ret = sd_bus_call(bus, req, 0, error, NULL);
	return (ret);
}
#endif /* WITH_SYSTEMD */
