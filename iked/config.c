/*	$OpenBSD: config.c,v 1.97 2024/02/15 19:11:00 tobhe Exp $	*/

/*
 * Copyright (c) 2019-2021 Tobias Heider <tobhe@openbsd.org>
 * Copyright (c) 2010-2013 Reyk Floeter <reyk@openbsd.org>
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
#include <sys/types.h>
#include <sys/uio.h>

#include <netinet/in.h>
#include <netinet/udp.h>

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <err.h>
#include <event.h>

#include <openssl/evp.h>
#include <openssl/pem.h>

#include "iked.h"
#include "ikev2.h"

struct iked_sa *
config_new_sa(struct iked *env, int initiator)
{
	struct iked_sa	*sa;

	if ((sa = calloc(1, sizeof(*sa))) == NULL)
		return (NULL);

	TAILQ_INIT(&sa->sa_proposals);
	TAILQ_INIT(&sa->sa_childsas);
	TAILQ_INIT(&sa->sa_flows);
	TAILQ_INIT(&sa->sa_requests);
	TAILQ_INIT(&sa->sa_responses);
	sa->sa_hdr.sh_initiator = initiator;
	sa->sa_type = IKED_SATYPE_LOCAL;

	if (initiator)
		sa->sa_hdr.sh_ispi = config_getspi();
	else
		sa->sa_hdr.sh_rspi = config_getspi();

	gettimeofday(&sa->sa_timecreated, NULL);
	memcpy(&sa->sa_timeused, &sa->sa_timecreated, sizeof(sa->sa_timeused));

	ikestat_inc(env, ikes_sa_created);
	return (sa);
}

uint64_t
config_getspi(void)
{
	uint64_t	 spi;

	do {
		arc4random_buf(&spi, sizeof spi);
	} while (spi == 0);

	return (spi);
}

void
config_free_kex(struct iked_kex *kex)
{
	if (kex == NULL)
		return;

	ibuf_free(kex->kex_inonce);
	ibuf_free(kex->kex_rnonce);

	group_free(kex->kex_dhgroup);
	ibuf_free(kex->kex_dhiexchange);
	ibuf_free(kex->kex_dhrexchange);

	free(kex);
}

void
config_free_fragments(struct iked_frag *frag)
{
	size_t i;

	if (frag && frag->frag_arr) {
		for (i = 0; i < frag->frag_total; i++) {
			if (frag->frag_arr[i] != NULL)
				free(frag->frag_arr[i]->frag_data);
			free(frag->frag_arr[i]);
		}
		free(frag->frag_arr);
		bzero(frag, sizeof(struct iked_frag));
	}
}

void
config_free_sa(struct iked *env, struct iked_sa *sa)
{
	int i;

	timer_del(env, &sa->sa_timer);
	timer_del(env, &sa->sa_keepalive);
	timer_del(env, &sa->sa_rekey);

	config_free_fragments(&sa->sa_fragments);
	config_free_proposals(&sa->sa_proposals, 0);
	config_free_childsas(env, &sa->sa_childsas, NULL, NULL);
	sa_configure_iface(env, sa, 0);
	sa_free_flows(env, &sa->sa_flows);

	if (sa->sa_addrpool) {
		(void)RB_REMOVE(iked_addrpool, &env->sc_addrpool, sa);
		free(sa->sa_addrpool);
	}
	if (sa->sa_addrpool6) {
		(void)RB_REMOVE(iked_addrpool6, &env->sc_addrpool6, sa);
		free(sa->sa_addrpool6);
	}

	if (sa->sa_policy) {
		TAILQ_REMOVE(&sa->sa_policy->pol_sapeers, sa, sa_peer_entry);
		policy_unref(env, sa->sa_policy);
	}

	ikev2_msg_flushqueue(env, &sa->sa_requests);
	ikev2_msg_flushqueue(env, &sa->sa_responses);

	ibuf_free(sa->sa_inonce);
	ibuf_free(sa->sa_rnonce);

	group_free(sa->sa_dhgroup);
	ibuf_free(sa->sa_dhiexchange);
	ibuf_free(sa->sa_dhrexchange);

	ibuf_free(sa->sa_simult);

	hash_free(sa->sa_prf);
	hash_free(sa->sa_integr);
	cipher_free(sa->sa_encr);

	ibuf_free(sa->sa_key_d);
	ibuf_free(sa->sa_key_iauth);
	ibuf_free(sa->sa_key_rauth);
	ibuf_free(sa->sa_key_iencr);
	ibuf_free(sa->sa_key_rencr);
	ibuf_free(sa->sa_key_iprf);
	ibuf_free(sa->sa_key_rprf);

	ibuf_free(sa->sa_1stmsg);
	ibuf_free(sa->sa_2ndmsg);

	ibuf_free(sa->sa_iid.id_buf);
	ibuf_free(sa->sa_rid.id_buf);
	ibuf_free(sa->sa_icert.id_buf);
	ibuf_free(sa->sa_rcert.id_buf);
	for (i = 0; i < IKED_SCERT_MAX; i++)
		ibuf_free(sa->sa_scert[i].id_buf);
	ibuf_free(sa->sa_localauth.id_buf);
	ibuf_free(sa->sa_peerauth.id_buf);

	ibuf_free(sa->sa_eap.id_buf);
	free(sa->sa_eapid);
	ibuf_free(sa->sa_eapmsk);

	free(sa->sa_cp_addr);
	free(sa->sa_cp_addr6);
	free(sa->sa_cp_dns);

	free(sa->sa_tag);

	if (sa->sa_state == IKEV2_STATE_ESTABLISHED)
		ikestat_dec(env, ikes_sa_established_current);
	ikestat_inc(env, ikes_sa_removed);

	free(sa);
}

struct iked_policy *
config_new_policy(struct iked *env)
{
	struct iked_policy	*pol;

	if ((pol = calloc(1, sizeof(*pol))) == NULL)
		return (NULL);

	/* XXX caller does this again */
	TAILQ_INIT(&pol->pol_proposals);
	TAILQ_INIT(&pol->pol_sapeers);
	TAILQ_INIT(&pol->pol_tssrc);
	TAILQ_INIT(&pol->pol_tsdst);
	RB_INIT(&pol->pol_flows);

	return (pol);
}

void
config_free_policy(struct iked *env, struct iked_policy *pol)
{
	struct iked_sa		*sa;
	struct iked_ts	*tsi;

	if (pol->pol_flags & IKED_POLICY_REFCNT)
		goto remove;

	/*
	 * Remove policy from the sc_policies list, but increment
	 * refcount for every SA linked for the policy.
	 */
	pol->pol_flags |= IKED_POLICY_REFCNT;

	TAILQ_REMOVE(&env->sc_policies, pol, pol_entry);

	TAILQ_FOREACH(sa, &pol->pol_sapeers, sa_peer_entry) {
		if (sa->sa_policy == pol)
			policy_ref(env, pol);
		else
			log_warnx("%s: ERROR: sa_policy %p != pol %p",
			    __func__, sa->sa_policy, pol);
	}

	if (pol->pol_refcnt)
		return;

 remove:
	while ((tsi = TAILQ_FIRST(&pol->pol_tssrc))) {
		TAILQ_REMOVE(&pol->pol_tssrc, tsi, ts_entry);
		free(tsi);
	}
	while ((tsi = TAILQ_FIRST(&pol->pol_tsdst))) {
		TAILQ_REMOVE(&pol->pol_tsdst, tsi, ts_entry);
		free(tsi);
	}
	config_free_proposals(&pol->pol_proposals, 0);
	config_free_flows(env, &pol->pol_flows);
	free(pol);
}

struct iked_proposal *
config_add_proposal(struct iked_proposals *head, unsigned int id,
    unsigned int proto)
{
	struct iked_proposal	*pp;

	TAILQ_FOREACH(pp, head, prop_entry) {
		if (pp->prop_protoid == proto &&
		    pp->prop_id == id)
			return (pp);
	}

	if ((pp = calloc(1, sizeof(*pp))) == NULL)
		return (NULL);

	pp->prop_protoid = proto;
	pp->prop_id = id;

	TAILQ_INSERT_TAIL(head, pp, prop_entry);

	return (pp);
}

void
config_free_proposal(struct iked_proposals *head, struct iked_proposal *prop)
{
	TAILQ_REMOVE(head, prop, prop_entry);
	if (prop->prop_nxforms)
		free(prop->prop_xforms);
	free(prop);
}

void
config_free_proposals(struct iked_proposals *head, unsigned int proto)
{
	struct iked_proposal	*prop, *proptmp;

	TAILQ_FOREACH_SAFE(prop, head, prop_entry, proptmp) {
		/* Free any proposal or only selected SA proto */
		if (proto != 0 && prop->prop_protoid != proto)
			continue;

		log_debug("%s: free %p", __func__, prop);

		config_free_proposal(head, prop);
	}
}

void
config_free_flows(struct iked *env, struct iked_flows *head)
{
	struct iked_flow	*flow;

	while ((flow = RB_MIN(iked_flows, head))) {
		log_debug("%s: free %p", __func__, flow);
		RB_REMOVE(iked_flows, head, flow);
		flow_free(flow);
	}
}

void
config_free_childsas(struct iked *env, struct iked_childsas *head,
    struct iked_spi *peerspi, struct iked_spi *localspi)
{
	struct iked_childsa	*csa, *csatmp, *ipcomp;

	if (localspi != NULL)
		bzero(localspi, sizeof(*localspi));

	TAILQ_FOREACH_SAFE(csa, head, csa_entry, csatmp) {
		if (peerspi != NULL) {
			/* Only delete matching peer SPIs */
			if (peerspi->spi != csa->csa_peerspi)
				continue;

			/* Store assigned local SPI */
			if (localspi != NULL && localspi->spi == 0)
				memcpy(localspi, &csa->csa_spi,
				    sizeof(*localspi));
		}
		log_debug("%s: free %p", __func__, csa);

		TAILQ_REMOVE(head, csa, csa_entry);
		if (csa->csa_loaded) {
			RB_REMOVE(iked_activesas, &env->sc_activesas, csa);
			(void)ipsec_sa_delete(env, csa);
		}
		if ((ipcomp = csa->csa_bundled) != NULL) {
			log_debug("%s: free IPCOMP %p", __func__, ipcomp);
			if (ipcomp->csa_loaded)
				(void)ipsec_sa_delete(env, ipcomp);
			childsa_free(ipcomp);
		}
		childsa_free(csa);
		ikestat_inc(env, ikes_csa_removed);
	}
}

int
config_add_transform(struct iked_proposal *prop, unsigned int type,
    unsigned int id, unsigned int length, unsigned int keylength)
{
	struct iked_transform	*xform;
	struct iked_constmap	*map = NULL;
	int			 score = 1;
	unsigned int		 i;

	switch (type) {
	case IKEV2_XFORMTYPE_ENCR:
		map = ikev2_xformencr_map;
		break;
	case IKEV2_XFORMTYPE_PRF:
		map = ikev2_xformprf_map;
		break;
	case IKEV2_XFORMTYPE_INTEGR:
		map = ikev2_xformauth_map;
		break;
	case IKEV2_XFORMTYPE_DH:
		map = ikev2_xformdh_map;
		break;
	case IKEV2_XFORMTYPE_ESN:
		map = ikev2_xformesn_map;
		break;
	default:
		log_debug("%s: invalid transform type %d", __func__, type);
		return (-2);
	}

	for (i = 0; i < prop->prop_nxforms; i++) {
		xform = prop->prop_xforms + i;
		if (xform->xform_type == type &&
		    xform->xform_id == id &&
		    xform->xform_length == length)
			return (0);
	}

	for (i = 0; i < prop->prop_nxforms; i++) {
		xform = prop->prop_xforms + i;
		if (xform->xform_type == type) {
			switch (type) {
			case IKEV2_XFORMTYPE_ENCR:
			case IKEV2_XFORMTYPE_INTEGR:
				score += 3;
				break;
			case IKEV2_XFORMTYPE_DH:
				score += 2;
				break;
			default:
				score += 1;
				break;
			}
		}
	}

	if ((xform = reallocarray(prop->prop_xforms,
	    prop->prop_nxforms + 1, sizeof(*xform))) == NULL) {
		return (-1);
	}

	prop->prop_xforms = xform;
	xform = prop->prop_xforms + prop->prop_nxforms++;
	bzero(xform, sizeof(*xform));

	xform->xform_type = type;
	xform->xform_id = id;
	xform->xform_length = length;
	xform->xform_keylength = keylength;
	xform->xform_score = score;
	xform->xform_map = map;

	return (0);
}

struct iked_transform *
config_findtransform_ext(struct iked_proposals *props, uint8_t type, int id,
    unsigned int proto)
{
	struct iked_proposal	*prop;
	struct iked_transform	*xform;
	unsigned int		 i;

	/* Search of the first transform with the desired type */
	TAILQ_FOREACH(prop, props, prop_entry) {
		/* Find any proposal or only selected SA proto */
		if (proto != 0 && prop->prop_protoid != proto)
			continue;
		for (i = 0; i < prop->prop_nxforms; i++) {
			xform = prop->prop_xforms + i;
			/* optional lookup of specific transform */
			if (id >= 0 && xform->xform_id != id)
				continue;
			if (xform->xform_type == type)
				return (xform);
		}
	}

	return (NULL);
}

struct iked_transform *
config_findtransform(struct iked_proposals *props, uint8_t type,
    unsigned int proto)
{
	return config_findtransform_ext(props, type, -1, proto);
}

struct iked_user *
config_new_user(struct iked *env, struct iked_user *new)
{
	struct iked_user	*usr, *old;

	if ((usr = calloc(1, sizeof(*usr))) == NULL)
		return (NULL);

	memcpy(usr, new, sizeof(*usr));

	if ((old = RB_INSERT(iked_users, &env->sc_users, usr)) != NULL) {
		/* Update the password of an existing user*/
		memcpy(old->usr_pass, new->usr_pass, IKED_PASSWORD_SIZE);

		log_debug("%s: updating user %s", __func__, usr->usr_name);
		freezero(usr, sizeof *usr);

		return (old);
	}

	log_debug("%s: inserting new user %s", __func__, usr->usr_name);
	return (usr);
}

/*
 * Inter-process communication of configuration items.
 */

int
config_setcoupled(struct iked *env, unsigned int couple)
{
	unsigned int	 type;

	type = couple ? IMSG_CTL_COUPLE : IMSG_CTL_DECOUPLE;
	proc_compose(&env->sc_ps, PROC_IKEV2, type, NULL, 0);

	return (0);
}

int
config_getcoupled(struct iked *env, unsigned int type)
{
	return (ipsec_couple(env, &env->sc_sas,
	    type == IMSG_CTL_COUPLE ? 1 : 0));
}

int
config_setmode(struct iked *env, unsigned int passive)
{
	unsigned int	 type;

	/*
	 * In order to control the startup of the processes,
	 * the messages are sent in this order:
	 *   PROC_PARENT -> PROC_CERT -> PROC_PARENT -> PROC_IKEV2
	 * so PROC_CERT is ready before PROC_IKEV2 is activated.
	 */
	type = passive ? IMSG_CTL_PASSIVE : IMSG_CTL_ACTIVE;
	proc_compose(&env->sc_ps, PROC_CERT, type, NULL, 0);

	return (0);
}

int
config_getmode(struct iked *env, unsigned int type)
{
	uint8_t		 old;
	unsigned char	*mode[] = { "active", "passive" };

	old = env->sc_passive ? 1 : 0;
	env->sc_passive = type == IMSG_CTL_PASSIVE ? 1 : 0;

	if (old == env->sc_passive)
		return (0);

	log_debug("%s: mode %s -> %s", __func__,
	    mode[old], mode[env->sc_passive]);

	return (0);
}

int
config_setreset(struct iked *env, unsigned int mode, enum privsep_procid id)
{
	proc_compose(&env->sc_ps, id, IMSG_CTL_RESET, &mode, sizeof(mode));
	return (0);
}

int
config_getreset(struct iked *env, struct imsg *imsg)
{
	unsigned int		 mode;

	IMSG_SIZE_CHECK(imsg, &mode);
	memcpy(&mode, imsg->data, sizeof(mode));

	return (config_doreset(env, mode));
}

int
config_doreset(struct iked *env, unsigned int mode)
{
	struct iked_policy	*pol, *poltmp;
	struct iked_sa		*sa;
	struct iked_user	*usr;

	if (mode == RESET_ALL || mode == RESET_POLICY) {
		log_debug("%s: flushing policies", __func__);
		TAILQ_FOREACH_SAFE(pol, &env->sc_policies, pol_entry, poltmp) {
			config_free_policy(env, pol);
		}
	}

	if (mode == RESET_ALL || mode == RESET_SA) {
		log_debug("%s: flushing SAs", __func__);
		while ((sa = RB_MIN(iked_sas, &env->sc_sas))) {
			/* for RESET_SA we try send a DELETE */
			if (mode == RESET_ALL ||
			    ikev2_ike_sa_delete(env, sa) != 0) {
				RB_REMOVE(iked_sas, &env->sc_sas, sa);
				if (sa->sa_dstid_entry_valid)
					sa_dstid_remove(env, sa);
				config_free_sa(env, sa);
			}
		}
	}

	if (mode == RESET_ALL || mode == RESET_USER) {
		log_debug("%s: flushing users", __func__);
		while ((usr = RB_MIN(iked_users, &env->sc_users))) {
			RB_REMOVE(iked_users, &env->sc_users, usr);
			free(usr);
		}
	}

	return (0);
}

/*
 * The first call of this function sets the UDP socket for IKEv2.
 * The second call is optional, setting the UDP socket used for NAT-T.
 */
int
config_setsocket(struct iked *env, struct sockaddr_storage *ss,
    in_port_t port, enum privsep_procid id, int natt)
{
	int	 s;

	if ((s = udp_bind((struct sockaddr *)ss, port)) == -1)
		return (-1);

#if defined(UDP_ENCAP_ESPINUDP)
	if (natt
#if !defined(HAVE_UDPENCAP6)
	    && ss->ss_family != AF_INET6
#endif
	    ) {
		int	 sopt;
		sopt = UDP_ENCAP_ESPINUDP;
		if (setsockopt(s, IPPROTO_UDP, UDP_ENCAP,
		    &sopt, sizeof(sopt)) < 0) {
			log_warn("%s: failed to set UDP encap socket option",
			    __func__);
			return (-1);
		}
	}
#endif

	proc_compose_imsg(&env->sc_ps, id, -1,
	    IMSG_UDP_SOCKET, -1, s, ss, sizeof(*ss));
	return (0);
}

int
config_getsocket(struct iked *env, struct imsg *imsg,
    void (*cb)(int, short, void *))
{
	struct iked_socket	*sock, **sock0 = NULL, **sock1 = NULL;

	if ((sock = calloc(1, sizeof(*sock))) == NULL)
		fatal("config_getsocket: calloc");

	IMSG_SIZE_CHECK(imsg, &sock->sock_addr);

	memcpy(&sock->sock_addr, imsg->data, sizeof(sock->sock_addr));
	sock->sock_fd = imsg_get_fd(imsg);
	sock->sock_env = env;

	log_debug("%s: received socket fd %d", __func__, sock->sock_fd);

	switch (sock->sock_addr.ss_family) {
	case AF_INET:
		sock0 = &env->sc_sock4[0];
		sock1 = &env->sc_sock4[1];
		break;
	case AF_INET6:
		sock0 = &env->sc_sock6[0];
		sock1 = &env->sc_sock6[1];
		break;
	default:
		fatal("config_getsocket: socket af: %u",
		    sock->sock_addr.ss_family);
		/* NOTREACHED */
	}
	if (*sock0 == NULL)
		*sock0 = sock;
	else if (*sock1 == NULL)
		*sock1 = sock;
	else
		fatalx("%s: too many call", __func__);

	event_set(&sock->sock_ev, sock->sock_fd,
	    EV_READ|EV_PERSIST, cb, sock);

	return (0);
}

void
config_enablesocket(struct iked *env)
{
	struct iked_socket	*sock;
	size_t			 i;

	for (i = 0; i < nitems(env->sc_sock4); i++)
		if ((sock = env->sc_sock4[i]) != NULL)
			event_add(&sock->sock_ev, NULL);
	for (i = 0; i < nitems(env->sc_sock6); i++)
		if ((sock = env->sc_sock6[i]) != NULL)
			event_add(&sock->sock_ev, NULL);
}

int
config_setpfkey(struct iked *env)
{
	int	 s;

	if ((s = ipsec_socket(env)) == -1)
		return (-1);
	proc_compose_imsg(&env->sc_ps, PROC_IKEV2, -1,
	    IMSG_PFKEY_SOCKET, -1, s, NULL, 0);
	return (0);
}

int
config_getpfkey(struct iked *env, struct imsg *imsg)
{
	int fd = imsg_get_fd(imsg);

	log_debug("%s: received pfkey fd %d", __func__, fd);
	ipsec_init(env, fd);
	return (0);
}

int
config_setuser(struct iked *env, struct iked_user *usr, enum privsep_procid id)
{
	if (env->sc_opts & IKED_OPT_NOACTION) {
		print_user(usr);
		return (0);
	}

	proc_compose(&env->sc_ps, id, IMSG_CFG_USER, usr, sizeof(*usr));
	return (0);
}

int
config_getuser(struct iked *env, struct imsg *imsg)
{
	struct iked_user	 usr;
	int			 ret = -1;

	IMSG_SIZE_CHECK(imsg, &usr);
	memcpy(&usr, imsg->data, sizeof(usr));

	if (config_new_user(env, &usr) != NULL) {
		print_user(&usr);
		ret = 0;
	}

	explicit_bzero(&usr, sizeof(usr));
	return (ret);
}

int
config_setpolicy(struct iked *env, struct iked_policy *pol,
    enum privsep_procid id)
{
	struct iked_proposal	*prop;
	struct iked_transform	*xform;
	size_t			 iovcnt, j, c = 0;
	struct iovec		 iov[IOV_MAX];

	iovcnt = 1;
	TAILQ_FOREACH(prop, &pol->pol_proposals, prop_entry) {
		iovcnt += prop->prop_nxforms + 1;
	}

	if (iovcnt > IOV_MAX) {
		log_warn("%s: too many proposals", __func__);
		return (-1);
	}

	iov[c].iov_base = pol;
	iov[c++].iov_len = sizeof(*pol);

	TAILQ_FOREACH(prop, &pol->pol_proposals, prop_entry) {
		iov[c].iov_base = prop;
		iov[c++].iov_len = sizeof(*prop);

		for (j = 0; j < prop->prop_nxforms; j++) {
			xform = prop->prop_xforms + j;

			iov[c].iov_base = xform;
			iov[c++].iov_len = sizeof(*xform);
		}
	}

	print_policy(pol);

	if (env->sc_opts & IKED_OPT_NOACTION)
		return (0);

	if (proc_composev(&env->sc_ps, id, IMSG_CFG_POLICY, iov,
	    iovcnt) == -1) {
		log_debug("%s: proc_composev failed", __func__);
		return (-1);
	}

	return (0);
}

int
config_setflow(struct iked *env, struct iked_policy *pol,
    enum privsep_procid id)
{
	struct iked_flow	*flow;
	struct iovec		 iov[2];

	if (env->sc_opts & IKED_OPT_NOACTION)
		return (0);

	RB_FOREACH(flow, iked_flows, &pol->pol_flows) {
		iov[0].iov_base = &pol->pol_id;
		iov[0].iov_len = sizeof(pol->pol_id);
		iov[1].iov_base = flow;
		iov[1].iov_len = sizeof(*flow);

		if (proc_composev(&env->sc_ps, id, IMSG_CFG_FLOW,
		    iov, 2) == -1) {
			log_debug("%s: proc_composev failed", __func__);
			return (-1);
		}
	}

	return (0);
}

int
config_getpolicy(struct iked *env, struct imsg *imsg)
{
	struct iked_policy	*pol;
	struct iked_proposal	 pp, *prop;
	struct iked_transform	 xf;
	off_t			 offset = 0;
	unsigned int		 i, j;
	uint8_t			*buf = (uint8_t *)imsg->data;

	IMSG_SIZE_CHECK(imsg, pol);
	log_debug("%s: received policy", __func__);

	if ((pol = config_new_policy(NULL)) == NULL)
		fatal("config_getpolicy: new policy");

	memcpy(pol, buf, sizeof(*pol));
	offset += sizeof(*pol);

	TAILQ_INIT(&pol->pol_tssrc);
	TAILQ_INIT(&pol->pol_tsdst);
	TAILQ_INIT(&pol->pol_proposals);
	TAILQ_INIT(&pol->pol_sapeers);
	RB_INIT(&pol->pol_flows);

	for (i = 0; i < pol->pol_nproposals; i++) {
		memcpy(&pp, buf + offset, sizeof(pp));
		offset += sizeof(pp);

		if ((prop = config_add_proposal(&pol->pol_proposals,
		    pp.prop_id, pp.prop_protoid)) == NULL)
			fatal("config_getpolicy: add proposal");

		for (j = 0; j < pp.prop_nxforms; j++) {
			memcpy(&xf, buf + offset, sizeof(xf));
			offset += sizeof(xf);

			if (config_add_transform(prop, xf.xform_type,
			    xf.xform_id, xf.xform_length,
			    xf.xform_keylength) != 0)
				fatal("config_getpolicy: add transform");
		}
	}

	/* Flows are sent separately */
	pol->pol_nflows = 0;

	TAILQ_INSERT_TAIL(&env->sc_policies, pol, pol_entry);

	if (pol->pol_flags & IKED_POLICY_DEFAULT) {
		/* Only one default policy, just free/unref the old one */
		if (env->sc_defaultcon != NULL)
			config_free_policy(env, env->sc_defaultcon);
		env->sc_defaultcon = pol;
	}

	return (0);
}

int
config_getflow(struct iked *env, struct imsg *imsg)
{
	struct iked_policy	*pol;
	struct iked_flow	*flow;
	off_t			 offset = 0;
	unsigned int		 id;
	uint8_t			*buf = (uint8_t *)imsg->data;

	if (IMSG_DATA_SIZE(imsg) < sizeof(id))
		fatalx("bad length imsg received");

	memcpy(&id, buf, sizeof(id));
	offset += sizeof(id);

	TAILQ_FOREACH(pol, &env->sc_policies, pol_entry) {
		if (pol->pol_id == id)
			break;
	}
	if (pol == NULL) {
		log_warnx("%s: unknown policy %u", __func__, id);
		return (-1);
	}

	if ((flow = calloc(1, sizeof(*flow))) == NULL)
		fatal("config_getpolicy: new flow");

	memcpy(flow, buf + offset, sizeof(*flow));

	if (RB_INSERT(iked_flows, &pol->pol_flows, flow)) {
		log_warnx("%s: received duplicate flow", __func__);
		free(flow);
		return (-1);
	}
	pol->pol_nflows++;

	return (0);
}

int
config_setcompile(struct iked *env, enum privsep_procid id)
{
	if (env->sc_opts & IKED_OPT_NOACTION)
		return (0);

	proc_compose(&env->sc_ps, id, IMSG_COMPILE, NULL, 0);
	return (0);
}

int
config_getcompile(struct iked *env)
{
	/*
	 * Do any necessary steps after configuration, for now we
	 * only need to compile the skip steps.
	 */
	policy_calc_skip_steps(&env->sc_policies);

	log_debug("%s: compilation done", __func__);
	return (0);
}

int
config_setstatic(struct iked *env)
{
	proc_compose(&env->sc_ps, PROC_IKEV2, IMSG_CTL_STATIC,
	    &env->sc_static, sizeof(env->sc_static));
	proc_compose(&env->sc_ps, PROC_CERT, IMSG_CTL_STATIC,
	    &env->sc_static, sizeof(env->sc_static));
	return (0);
}

int
config_getstatic(struct iked *env, struct imsg *imsg)
{
	IMSG_SIZE_CHECK(imsg, &env->sc_static);
	memcpy(&env->sc_static, imsg->data, sizeof(env->sc_static));

	log_debug("%s: dpd_check_interval %llu", __func__,
	    (long long unsigned)env->sc_alive_timeout);
	log_debug("%s: %senforcesingleikesa", __func__,
	    env->sc_enforcesingleikesa ? "" : "no ");
	log_debug("%s: %sfragmentation", __func__, env->sc_frag ? "" : "no ");
	log_debug("%s: %smobike", __func__, env->sc_mobike ? "" : "no ");
	log_debug("%s: nattport %u", __func__, env->sc_nattport);
	log_debug("%s: %sstickyaddress", __func__,
	    env->sc_stickyaddress ? "" : "no ");

	ikev2_reset_alive_timer(env);

	return (0);
}

int
config_setocsp(struct iked *env)
{
	struct iovec		 iov[3];
	int			 iovcnt = 0;

	if (env->sc_opts & IKED_OPT_NOACTION)
		return (0);

	iov[0].iov_base = &env->sc_ocsp_tolerate;
	iov[0].iov_len = sizeof(env->sc_ocsp_tolerate);
	iovcnt++;
	iov[1].iov_base = &env->sc_ocsp_maxage;
	iov[1].iov_len = sizeof(env->sc_ocsp_maxage);
	iovcnt++;
	if (env->sc_ocsp_url) {
		iov[2].iov_base = env->sc_ocsp_url;
		iov[2].iov_len = strlen(env->sc_ocsp_url);
		iovcnt++;
	}
	return (proc_composev(&env->sc_ps, PROC_CERT, IMSG_OCSP_CFG,
	    iov, iovcnt));
}

int
config_getocsp(struct iked *env, struct imsg *imsg)
{
	size_t		 have, need;
	uint8_t		*ptr;

	free(env->sc_ocsp_url);
	ptr = (uint8_t *)imsg->data;
	have = IMSG_DATA_SIZE(imsg);

	/* get tolerate */
	need = sizeof(env->sc_ocsp_tolerate);
	if (have < need)
		fatalx("bad 'tolerate' length imsg received");
	memcpy(&env->sc_ocsp_tolerate, ptr, need);
	ptr += need;
	have -= need;

	/* get maxage */
	need = sizeof(env->sc_ocsp_maxage);
	if (have < need)
		fatalx("bad 'maxage' length imsg received");
	memcpy(&env->sc_ocsp_maxage, ptr, need);
	ptr += need;
	have -= need;

	/* get url */
	if (have > 0)
		env->sc_ocsp_url = get_string(ptr, have);
	else
		env->sc_ocsp_url = NULL;
	log_debug("%s: ocsp_url %s tolerate %ld maxage %ld", __func__,
	    env->sc_ocsp_url ? env->sc_ocsp_url : "none",
	    env->sc_ocsp_tolerate, env->sc_ocsp_maxage);
	return (0);
}

int
config_setkeys(struct iked *env)
{
	FILE			*fp = NULL;
	EVP_PKEY		*key = NULL;
	struct iked_id		 privkey;
	struct iked_id		 pubkey;
	struct iovec		 iov[2];
	int			 ret = -1;

	memset(&privkey, 0, sizeof(privkey));
	memset(&pubkey, 0, sizeof(pubkey));

	/* Read private key */
	if ((fp = fopen(IKED_PRIVKEY, "r")) == NULL) {
		log_warn("%s: failed to open private key", __func__);
		goto done;
	}

	if ((key = PEM_read_PrivateKey(fp, NULL, NULL, NULL)) == NULL) {
		log_warnx("%s: failed to read private key", __func__);
		goto done;
	}

	if (ca_privkey_serialize(key, &privkey) != 0) {
		log_warnx("%s: failed to serialize private key", __func__);
		goto done;
	}
	if (ca_pubkey_serialize(key, &pubkey) != 0) {
		log_warnx("%s: failed to serialize public key", __func__);
		goto done;
	}

	iov[0].iov_base = &privkey;
	iov[0].iov_len = sizeof(privkey);
	iov[1].iov_base = ibuf_data(privkey.id_buf);
	iov[1].iov_len = ibuf_size(privkey.id_buf);

	if (proc_composev(&env->sc_ps, PROC_CERT, IMSG_PRIVKEY, iov, 2) == -1) {
		log_warnx("%s: failed to send private key", __func__);
		goto done;
	}

	iov[0].iov_base = &pubkey;
	iov[0].iov_len = sizeof(pubkey);
	iov[1].iov_base = ibuf_data(pubkey.id_buf);
	iov[1].iov_len = ibuf_size(pubkey.id_buf);

	if (proc_composev(&env->sc_ps, PROC_CERT, IMSG_PUBKEY, iov, 2) == -1) {
		log_warnx("%s: failed to send public key", __func__);
		goto done;
	}

	ret = 0;
 done:
	if (fp != NULL)
		fclose(fp);

	ibuf_free(pubkey.id_buf);
	ibuf_free(privkey.id_buf);
	EVP_PKEY_free(key);

	return (ret);
}

int
config_getkey(struct iked *env, struct imsg *imsg)
{
	size_t		 len;
	struct iked_id	 id;

	len = IMSG_DATA_SIZE(imsg);
	if (len <= sizeof(id))
		fatalx("%s: invalid key message", __func__);

	memcpy(&id, imsg->data, sizeof(id));
	if ((id.id_buf = ibuf_new((uint8_t *)imsg->data + sizeof(id),
	    len - sizeof(id))) == NULL)
		fatalx("%s: failed to get key", __func__);

	explicit_bzero(imsg->data, len);
	ca_getkey(&env->sc_ps, &id, imsg->hdr.type);

	return (0);
}
