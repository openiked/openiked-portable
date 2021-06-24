/*	$OpenBSD: vroute.c,v 1.11 2021/06/23 12:11:40 tobhe Exp $	*/

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

#include <net/if.h>
#include <net/route.h>
#include <netinet/in.h>
#include <netinet6/in6_var.h>
#include <netinet6/nd6.h>

#include <event.h>
#include <err.h>
#include <errno.h>
#include <poll.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <netdb.h>

#include <iked.h>

#define IKED_VROUTE_PRIO	6

#define ROUNDUP(a) (a>0 ? (1 + (((a) - 1) | (sizeof(long) - 1))) : sizeof(long))

int vroute_setroute(struct iked *, uint8_t, struct sockaddr *, uint8_t,
    struct sockaddr *, int);
int vroute_doroute(struct iked *, int, int, int, uint8_t, struct sockaddr *,
    struct sockaddr *, struct sockaddr *, int *);
int vroute_doaddr(struct iked *, char *, struct sockaddr *, struct sockaddr *, int);
#if defined(__FreeBSD__) || defined(__NetBSD__) || defined(__APPLE__)
static int vroute_dodefaultroute(struct iked *, int, int, uint8_t,
    struct sockaddr *);
#endif

void vroute_cleanup(struct iked *);

void vroute_insertaddr(struct iked *, int, struct sockaddr *, struct sockaddr *);
void vroute_removeaddr(struct iked *, int, struct sockaddr *, struct sockaddr *);
void vroute_insertroute(struct iked *, int, struct sockaddr *, struct sockaddr *);
void vroute_removeroute(struct iked *, int, struct sockaddr *, struct sockaddr *);

struct vroute_addr {
	int				va_ifidx;
	struct	sockaddr_storage	va_addr;
	struct	sockaddr_storage	va_mask;
	TAILQ_ENTRY(vroute_addr)	va_entry;
};
TAILQ_HEAD(vroute_addrs, vroute_addr);

struct vroute_route {
	int				vr_rdomain;
	int				vr_flags;
	struct	sockaddr_storage	vr_dest;
	struct	sockaddr_storage	vr_mask;
	TAILQ_ENTRY(vroute_route)	vr_entry;
};
TAILQ_HEAD(vroute_routes, vroute_route);

struct iked_vroute_sc {
	struct vroute_addrs	ivr_addrs;
	struct vroute_routes	ivr_routes;
	int			ivr_iosock;
	int			ivr_iosock6;
	int			ivr_rtsock;
	int			ivr_rtseq;
	pid_t			ivr_pid;
};

struct vroute_msg {
	struct rt_msghdr	 vm_rtm;
	uint8_t			 vm_space[512];
};

int vroute_process(struct iked *, int msglen, struct vroute_msg *,
    struct sockaddr *, struct sockaddr *, struct sockaddr *, int *);

void
vroute_init(struct iked *env)
{
	struct iked_vroute_sc	*ivr;

	ivr = calloc(1, sizeof(*ivr));
	if (ivr == NULL)
		fatal("%s: calloc.", __func__);

	if ((ivr->ivr_iosock = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
		fatal("%s: failed to create ioctl socket", __func__);

	if ((ivr->ivr_iosock6 = socket(AF_INET6, SOCK_DGRAM, 0)) == -1)
		fatal("%s: failed to create ioctl socket", __func__);

	if ((ivr->ivr_rtsock = socket(AF_ROUTE, SOCK_RAW, AF_UNSPEC)) == -1)
		fatal("%s: failed to create routing socket", __func__);

	TAILQ_INIT(&ivr->ivr_addrs);
	TAILQ_INIT(&ivr->ivr_routes);

	ivr->ivr_pid = getpid();

	env->sc_vroute = ivr;
}

void
vroute_cleanup(struct iked *env)
{
	char			 ifname[IF_NAMESIZE];
	struct iked_vroute_sc	*ivr = env->sc_vroute;
	struct vroute_addr	*addr;
	struct vroute_route	*route;

	while ((addr = TAILQ_FIRST(&ivr->ivr_addrs))) {
		if_indextoname(addr->va_ifidx, ifname);
		vroute_doaddr(env, ifname,
		    (struct sockaddr *)&addr->va_addr,
		    (struct sockaddr *)&addr->va_mask, 0);
		TAILQ_REMOVE(&ivr->ivr_addrs, addr, va_entry);
		free(addr);
	}

	while ((route = TAILQ_FIRST(&ivr->ivr_routes))) {
		vroute_doroute(env, RTF_UP | RTF_GATEWAY | RTF_STATIC,
		    route->vr_flags, route->vr_rdomain, RTM_DELETE,
		    (struct sockaddr *)&route->vr_dest,
		    (struct sockaddr *)&route->vr_mask,
		    NULL, NULL);
		TAILQ_REMOVE(&ivr->ivr_routes, route, vr_entry);
		free(route);
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
	iov[0].iov_len = addr->sa_len;
	iovcnt++;

	switch(addr->sa_family) {
	case AF_INET:
		bzero(&mask, sizeof(mask));
		mask4.sin_addr.s_addr = prefixlen2mask(mask ? mask : 32);
		mask4.sin_family = AF_INET;
		mask4.sin_len = sizeof(mask4);

		iov[1].iov_base = &mask4;
		iov[1].iov_len = sizeof(mask4);
		iovcnt++;
		break;
	case AF_INET6:
		bzero(&mask6, sizeof(mask6));
		prefixlen2mask6(mask ? mask : 128,
		    (uint32_t *)&mask6.sin6_addr.s6_addr);
		mask6.sin6_family = AF_INET6;
		mask6.sin6_len = sizeof(mask6);
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
	char			 ifname[IF_NAMESIZE];
	struct sockaddr	*addr, *mask;
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

	if (left < addr->sa_len)
		fatalx("bad length imsg received");
	ptr += addr->sa_len;
	left -= addr->sa_len;

	if (left < sizeof(*mask))
		fatalx("bad length imsg received");
	mask = (struct sockaddr *) ptr;
	if (mask->sa_family != af)
		return (-1);

	if (left < mask->sa_len)
		fatalx("bad length imsg received");
	ptr += mask->sa_len;
	left -= mask->sa_len;

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

	if_indextoname(ifidx, ifname);
	return (vroute_doaddr(env, ifname, addr, mask, add));
}

void
vroute_insertroute(struct iked *env, int rdomain, struct sockaddr *dest,
    struct sockaddr *mask)
{
	struct iked_vroute_sc	*ivr = env->sc_vroute;
	struct vroute_route	*route;

	route = calloc(1, sizeof(*route));
	if (route == NULL)
		fatalx("%s: calloc.", __func__);

	if (dest != NULL) {
		route->vr_flags |= RTA_DST;
		memcpy(&route->vr_dest, dest, dest->sa_len);
	}
	if (mask != NULL) {
		route->vr_flags |= RTA_NETMASK;
		memcpy(&route->vr_mask, mask, mask->sa_len);
	}
	route->vr_rdomain = rdomain;

	TAILQ_INSERT_TAIL(&ivr->ivr_routes, route, vr_entry);
}

void
vroute_removeroute(struct iked *env, int rdomain, struct sockaddr *dest,
    struct sockaddr *mask)
{
	struct iked_vroute_sc	*ivr = env->sc_vroute;
	struct vroute_route	*route;

	TAILQ_FOREACH(route, &ivr->ivr_routes, vr_entry) {
		if (sockaddr_cmp(dest, (struct sockaddr *)&route->vr_dest, -1))
			continue;
		if (mask && !(route->vr_flags & RTA_NETMASK))
			continue;
		if (mask &&
		    sockaddr_cmp(mask, (struct sockaddr *)&route->vr_mask, -1))
			continue;
		if (rdomain != route->vr_rdomain)
			continue;
		TAILQ_REMOVE(&ivr->ivr_routes, route, vr_entry);
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

	memcpy(&vaddr->va_addr, addr, addr->sa_len);
	memcpy(&vaddr->va_mask, mask, mask->sa_len);
	vaddr->va_ifidx = ifidx;

	TAILQ_INSERT_TAIL(&ivr->ivr_addrs, vaddr, va_entry);
}

void
vroute_removeaddr(struct iked *env, int ifidx, struct sockaddr *addr,
    struct sockaddr *mask)
{
	struct iked_vroute_sc	*ivr = env->sc_vroute;
	struct vroute_addr	*vaddr;

	TAILQ_FOREACH(vaddr, &ivr->ivr_addrs, va_entry) {
		if (sockaddr_cmp(addr, (struct sockaddr *)&vaddr->va_addr, -1))
			continue;
		if (sockaddr_cmp(mask, (struct sockaddr *)&vaddr->va_mask, -1))
			continue;
		if (ifidx != vaddr->va_ifidx)
			continue;
		TAILQ_REMOVE(&ivr->ivr_addrs, vaddr, va_entry);
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
vroute_setroute(struct iked *env, uint8_t rdomain, struct sockaddr *dst,
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
	iov[iovcnt].iov_len = dst->sa_len;
	iovcnt++;

	if (type != IMSG_VROUTE_CLONE && addr) {
		bzero(&sa, sizeof(sa));
		switch(af) {
		case AF_INET:
			in = (struct sockaddr_in *)&sa;
			in->sin_addr.s_addr = prefixlen2mask(mask);
			in->sin_family = af;
			in->sin_len = sizeof(*in);
			iov[iovcnt].iov_base = in;
			iov[iovcnt].iov_len = sizeof(*in);
			iovcnt++;
			break;
		case AF_INET6:
			in6 = (struct sockaddr_in6 *)&sa;
			prefixlen2mask6(mask,
			    (uint32_t *)in6->sin6_addr.s6_addr);
			in6->sin6_family = af;
			in6->sin6_len = sizeof(*in6);
			iov[iovcnt].iov_base = in6;
			iov[iovcnt].iov_len = sizeof(*in6);
			iovcnt++;
			break;
		}

		iov[iovcnt].iov_base = addr;
		iov[iovcnt].iov_len = addr->sa_len;
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
	int			 addrs = 0;
	int			 type, flags;
	uint8_t			 rdomain;

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
	if (left < dest->sa_len)
		return (-1);
	socket_setport(dest, 0);
	ptr += dest->sa_len;
	left -= dest->sa_len;
	addrs |= RTA_DST;

	flags = RTF_UP | RTF_GATEWAY | RTF_STATIC;
	if (left != 0) {
		if (left < sizeof(struct sockaddr))
			return (-1);
		mask = (struct sockaddr *)ptr;
		if (left < mask->sa_len)
			return (-1);
		socket_setport(mask, 0);
		ptr += mask->sa_len;
		left -= mask->sa_len;
		addrs |= RTA_NETMASK;

		if (left < sizeof(struct sockaddr))
			return (-1);
		gateway = (struct sockaddr *)ptr;
		if (left < gateway->sa_len)
			return (-1);
		socket_setport(gateway, 0);
		ptr += gateway->sa_len;
		left -= gateway->sa_len;
		addrs |= RTA_GATEWAY;
	} else {
		flags |= RTF_HOST;
	}

	switch(imsg->hdr.type) {
	case IMSG_VROUTE_ADD:
		type = RTM_ADD;
		break;
	case IMSG_VROUTE_DEL:
		type = RTM_DELETE;
		break;
	}

#if defined(__FreeBSD__) || defined(__NetBSD__) || defined(__APPLE__)
	if (mask && mask2prefixlen(mask) == 0) {
		return (vroute_dodefaultroute(env, flags, addrs, type, gateway));
	}
#endif
	if (type == RTM_ADD)
		vroute_insertroute(env, rdomain, dest, mask);
	else
		vroute_removeroute(env, rdomain, dest, mask);
	return (vroute_doroute(env, flags, addrs, rdomain, type,
	    dest, mask, gateway, NULL));
}

int
vroute_getcloneroute(struct iked *env, struct imsg *imsg)
{
	struct sockaddr		*dst;
	struct sockaddr_storage	 dest;
	struct sockaddr_storage	 mask;
	struct sockaddr_storage	 addr;
	uint8_t			*ptr;
	size_t			 left;
	uint8_t			 rdomain;
	int			 flags;
	int			 addrs;
	int			 need_gw;

	ptr = (uint8_t *)imsg->data;
	left = IMSG_DATA_SIZE(imsg);

	if (left < sizeof(rdomain))
		return (-1);
	rdomain = *ptr;
	ptr += sizeof(rdomain);
	left -= sizeof(rdomain);

	bzero(&dest, sizeof(dest));
	bzero(&mask, sizeof(mask));
	bzero(&addr, sizeof(addr));

	if (left < sizeof(struct sockaddr))
		return (-1);
	dst = (struct sockaddr *)ptr;
	if (left < dst->sa_len)
		return (-1);
	memcpy(&dest, dst, dst->sa_len);
	ptr += dst->sa_len;
	left -= dst->sa_len;

	/* Get route to peer */
	flags = RTF_UP | RTF_HOST | RTF_STATIC;
	if (vroute_doroute(env, flags, RTA_DST, rdomain, RTM_GET,
	    (struct sockaddr *)&dest, (struct sockaddr *)&mask,
	    (struct sockaddr *)&addr, &need_gw))
		return (-1);

	if (need_gw)
		flags |= RTF_GATEWAY;

	memcpy(&dest, dst, dst->sa_len);
	socket_setport((struct sockaddr *)&dest, 0);
	vroute_insertroute(env, rdomain, (struct sockaddr *)&dest, NULL);

	/* Set explicit route to peer with gateway addr*/
	addrs = RTA_DST | RTA_GATEWAY | RTA_NETMASK;
	return (vroute_doroute(env, flags, addrs, rdomain, RTM_ADD,
	    (struct sockaddr *)&dest, (struct sockaddr *)&mask,
	    (struct sockaddr *)&addr, NULL));
}

int
vroute_doroute(struct iked *env, int flags, int addrs, int rdomain, uint8_t type,
    struct sockaddr *dest, struct sockaddr *mask, struct sockaddr *addr, int *need_gw)
{
	struct vroute_msg	 m_rtmsg;
	struct iovec		 iov[7];
	struct iked_vroute_sc	*ivr = env->sc_vroute;
	ssize_t			 len;
	int			 iovcnt = 0;
	int			 i;
	long			 pad = 0;
	size_t			 padlen;

	bzero(&m_rtmsg, sizeof(m_rtmsg));
#define rtm m_rtmsg.vm_rtm
	rtm.rtm_version = RTM_VERSION;
	rtm.rtm_type = type;
	rtm.rtm_seq = ++ivr->ivr_rtseq;
#ifdef __OpenBSD__
	rtm.rtm_tableid = rdomain;
	if (type != RTM_GET)
		rtm.rtm_priority = IKED_VROUTE_PRIO;
#endif
	rtm.rtm_flags = flags;
	rtm.rtm_addrs = addrs;

	iov[iovcnt].iov_base = &rtm;
	iov[iovcnt].iov_len = sizeof(rtm);
	iovcnt++;

	if (rtm.rtm_addrs & RTA_DST) {
		iov[iovcnt].iov_base = dest;
		iov[iovcnt].iov_len = dest->sa_len;
		iovcnt++;
		padlen = ROUNDUP(dest->sa_len) - dest->sa_len;
		if (padlen > 0) {
			iov[iovcnt].iov_base = &pad;
			iov[iovcnt].iov_len = padlen;
			iovcnt++;
		}
	}

	if (rtm.rtm_addrs & RTA_GATEWAY) {
		iov[iovcnt].iov_base = addr;
		iov[iovcnt].iov_len = addr->sa_len;
		iovcnt++;
		padlen = ROUNDUP(addr->sa_len) - addr->sa_len;
		if (padlen > 0) {
			iov[iovcnt].iov_base = &pad;
			iov[iovcnt].iov_len = padlen;
			iovcnt++;
		}
	}

	if (rtm.rtm_addrs & RTA_NETMASK) {
		iov[iovcnt].iov_base = mask;
		iov[iovcnt].iov_len = mask->sa_len;
		iovcnt++;
		padlen = ROUNDUP(mask->sa_len) - mask->sa_len;
		if (padlen > 0) {
			iov[iovcnt].iov_base = &pad;
			iov[iovcnt].iov_len = padlen;
			iovcnt++;
		}
	}

	for (i = 0; i < iovcnt; i++)
		rtm.rtm_msglen += iov[i].iov_len;

	log_debug("%s: len: %u type: %s rdomain: %d flags %x addrs %x", __func__, rtm.rtm_msglen,
	    type == RTM_ADD ? "RTM_ADD" : type == RTM_DELETE ? "RTM_DELETE" :
	    type == RTM_GET ? "RTM_GET" : "unknown", rdomain, flags,  addrs);

	if (writev(ivr->ivr_rtsock, iov, iovcnt) == -1) {
		if ((type == RTM_ADD && errno != EEXIST) ||
		    (type == RTM_DELETE && errno != ESRCH)) {
			log_warn("%s: write %d", __func__, rtm.rtm_errno);
			return (0);
		}
	}

	if (type == RTM_GET) {
		do {
			len = read(ivr->ivr_rtsock, &m_rtmsg, sizeof(m_rtmsg));
		} while(len > 0 && (rtm.rtm_version != RTM_VERSION ||
		    rtm.rtm_seq != ivr->ivr_rtseq || rtm.rtm_pid != ivr->ivr_pid));
		return (vroute_process(env, len, &m_rtmsg, dest, mask, addr, need_gw));
	}
#undef rtm

	return (0);
}

#if defined(__FreeBSD__) || defined(__NetBSD__) || defined(__APPLE__)
static int
vroute_dodefaultroute(struct iked *env, int flags, int addrs, uint8_t type,
    struct sockaddr *gateway)
{
	struct sockaddr_storage	 dest, mask;
	struct sockaddr_in	*in;
	struct sockaddr_in6	*in6;
	int ret;
	int af;

	bzero(&dest, sizeof(dest));
	bzero(&mask, sizeof(mask));

	/* 0/1 */
	af = gateway->sa_family;
	switch (af) {
	case AF_INET:
		in = (struct sockaddr_in *)&dest;
		in->sin_addr.s_addr = INADDR_ANY;
		in->sin_family = af;
		in->sin_len = sizeof(*in);

		in = (struct sockaddr_in *)&mask;
		in->sin_addr.s_addr = prefixlen2mask(1);
		in->sin_family = af;
		in->sin_len = sizeof(*in);
		break;
	case AF_INET6:
		in6 = (struct sockaddr_in6 *)&dest;
		in6->sin6_addr = in6addr_any;
		in6->sin6_family = af;
		in6->sin6_len = sizeof(*in6);

		in6 = (struct sockaddr_in6 *)&mask;
		prefixlen2mask6(1,
		    (uint32_t *)in6->sin6_addr.s6_addr);
		in6->sin6_family = af;
		in6->sin6_len = sizeof(*in6);
		break;
	default:
		return (-1);
	}

	if (type == RTM_ADD)
		vroute_insertroute(env, 0, (struct sockaddr *)&dest,
		    (struct sockaddr *)&mask);
	else
		vroute_removeroute(env, 0, (struct sockaddr *)&dest,
		    (struct sockaddr *)&mask);
	ret = vroute_doroute(env, flags, addrs, 0, type,
	    (struct sockaddr *)&dest, (struct sockaddr *)&mask, gateway, NULL);
	if (ret)
		return (-1);

	/* 128/1 */
	bzero(&dest, sizeof(dest));
	switch (af) {
	case AF_INET:
		in = (struct sockaddr_in *)&dest;
		in->sin_addr.s_addr = htonl(0x80000000);
		in->sin_family = af;
		in->sin_len = sizeof(*in);
		break;
	case AF_INET6:
		in6 = (struct sockaddr_in6 *)&dest;
		in6->sin6_addr.s6_addr[0] = 0x80;
		in6->sin6_family = af;
		in6->sin6_len = sizeof(*in6);
		break;
	}

	if (type == RTM_ADD)
		vroute_insertroute(env, 0, (struct sockaddr *)&dest,
		    (struct sockaddr *)&mask);
	else
		vroute_removeroute(env, 0, (struct sockaddr *)&dest,
		    (struct sockaddr *)&mask);
	return (vroute_doroute(env, flags, addrs, 0, type,
	    (struct sockaddr *)&dest, (struct sockaddr *)&mask, gateway, NULL));
}
#endif

int
vroute_process(struct iked *env, int msglen, struct vroute_msg *m_rtmsg,
    struct sockaddr *dest, struct sockaddr *mask, struct sockaddr *addr, int *need_gw)
{
	struct sockaddr *sa;
	char *cp;
	int i;

#define rtm m_rtmsg->vm_rtm
	if (rtm.rtm_version != RTM_VERSION) {
		warnx("routing message version %u not understood",
		    rtm.rtm_version);
		return (-1);
	}
	if (rtm.rtm_msglen > msglen) {
		warnx("message length mismatch, in packet %u, returned %d",
		    rtm.rtm_msglen, msglen);
		return (-1);
	}
	if (rtm.rtm_errno) {
		warnx("RTM_GET: %s (errno %d)",
		    strerror(rtm.rtm_errno), rtm.rtm_errno);
		return (-1);
	}
	cp = m_rtmsg->vm_space;
	*need_gw = rtm.rtm_flags & RTF_GATEWAY;
	if(rtm.rtm_addrs) {
		for (i = 1; i; i <<= 1) {
			if (i & rtm.rtm_addrs) {
				sa = (struct sockaddr *)cp;
				switch(i) {
				case RTA_DST:
					memcpy(dest, cp, sa->sa_len);
					break;
				case RTA_NETMASK:
					memcpy(mask, cp, sa->sa_len);
					break;
				case RTA_GATEWAY:
					memcpy(addr, cp, sa->sa_len);
					break;
				}
				cp += ROUNDUP(sa->sa_len);
			}
		}
	}
#undef rtm
	return (0);
}

int
vroute_doaddr(struct iked *env, char *ifname, struct sockaddr *addr,
    struct sockaddr *mask, int add)
{
	struct iked_vroute_sc	*ivr = env->sc_vroute;
	struct ifaliasreq	 req;
	struct in6_aliasreq	 req6;
	unsigned long		 ioreq;
	int			 af;
	char			 addr_buf[NI_MAXHOST];
	char			 mask_buf[NI_MAXHOST];

	af = addr->sa_family;
	switch (af) {
	case AF_INET:
		bzero(&req, sizeof(req));
		strncpy(req.ifra_name, ifname, sizeof(req.ifra_name));
		memcpy(&req.ifra_addr, addr, sizeof(req.ifra_addr));
		if (add)
			memcpy(&req.ifra_mask, mask, sizeof(req.ifra_addr));

		inet_ntop(af, &((struct sockaddr_in *)addr)->sin_addr,
		    addr_buf, sizeof(addr_buf));
		inet_ntop(af, &((struct sockaddr_in *)mask)->sin_addr,
		    mask_buf, sizeof(mask_buf));
		log_debug("%s: %s inet %s netmask %s", __func__,
		    add ? "add" : "del",addr_buf, mask_buf);

		ioreq = add ? SIOCAIFADDR : SIOCDIFADDR;
		if (ioctl(ivr->ivr_iosock, ioreq, &req) == -1) {
			log_warn("%s: req: %lu", __func__, ioreq);
			return (-1);
		}
		break;
	case AF_INET6:
		bzero(&req6, sizeof(req6));
		strncpy(req6.ifra_name, ifname, sizeof(req6.ifra_name));
		req6.ifra_lifetime.ia6t_pltime = ND6_INFINITE_LIFETIME;
		req6.ifra_lifetime.ia6t_vltime = ND6_INFINITE_LIFETIME;

		memcpy(&req6.ifra_addr, addr, sizeof(req6.ifra_addr));
		if (add)
			memcpy(&req6.ifra_prefixmask, mask,
			    sizeof(req6.ifra_prefixmask));

		inet_ntop(af, &((struct sockaddr_in6 *)addr)->sin6_addr,
		    addr_buf, sizeof(addr_buf));
		inet_ntop(af, &((struct sockaddr_in6 *)mask)->sin6_addr,
		    mask_buf, sizeof(mask_buf));
		log_debug("%s: %s inet6 %s netmask %s", __func__,
		    add ? "add" : "del",addr_buf, mask_buf);

		ioreq = add ? SIOCAIFADDR_IN6 : SIOCDIFADDR_IN6;
		if (ioctl(ivr->ivr_iosock6, ioreq, &req6) == -1) {
			log_warn("%s: req: %lu", __func__, ioreq);
			return (-1);
		}
		break;
	default:
		return (-1);
	}

	return (0);
}
