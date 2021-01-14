/*	$OpenBSD: ca.c,v 1.75 2020/12/05 19:10:47 tobhe Exp $	*/

/*
 * Copyright (c) 2020-2021 Tobias Heider <tobhe@openbsd.org>
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
#include <sys/uio.h>

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <signal.h>
#include <syslog.h>
#include <errno.h>
#include <err.h>
#include <event.h>

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/engine.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/pem.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <openssl/rsa.h>

#include "iked.h"
#include "ikev2.h"

void	 ca_run(struct privsep *, struct privsep_proc *, void *);
void	 ca_shutdown(struct privsep_proc *);
void	 ca_reset(struct privsep *);
int	 ca_reload(struct iked *);

int	 ca_getreq(struct iked *, struct imsg *);
int	 ca_getcert(struct iked *, struct imsg *);
int	 ca_getauth(struct iked *, struct imsg *);
X509	*ca_by_subjectpubkey(X509_STORE *, uint8_t *, size_t);
X509	*ca_by_issuer(X509_STORE *, X509_NAME *, struct iked_static_id *);
X509	*ca_by_subjectaltname(X509_STORE *, struct iked_static_id *);
void	 ca_store_certs_info(const char *, X509_STORE *);
int	 ca_subjectpubkey_digest(X509 *, uint8_t *, unsigned int *);
int	 ca_x509_subject_cmp(X509 *, struct iked_static_id *);
int	 ca_validate_pubkey(struct iked *, struct iked_static_id *,
	    void *, size_t, struct iked_id *);
int	 ca_validate_cert(struct iked *, struct iked_static_id *,
	    void *, size_t, X509 **);
int	 ca_privkey_to_method(struct iked_id *);
struct ibuf *
	 ca_x509_serialize(X509 *);
int	 ca_x509_subjectaltname_do(X509 *, int, const char *,
	    struct iked_static_id *, struct iked_id *);
int	 ca_x509_subjectaltname_cmp(X509 *, struct iked_static_id *);
int	 ca_x509_subjectaltname_log(X509 *, const char *);
int	 ca_x509_subjectaltname_get(X509 *cert, struct iked_id *);
int	 ca_dispatch_parent(int, struct privsep_proc *, struct imsg *);
int	 ca_dispatch_ikev2(int, struct privsep_proc *, struct imsg *);

static struct privsep_proc procs[] = {
	{ "parent",	PROC_PARENT,	ca_dispatch_parent },
	{ "ikev2",	PROC_IKEV2,	ca_dispatch_ikev2 }
};

struct ca_store {
	X509_STORE	*ca_cas;
	X509_LOOKUP	*ca_calookup;

	X509_STORE	*ca_certs;
	X509_LOOKUP	*ca_certlookup;

	struct iked_id	 ca_privkey;
	struct iked_id	 ca_pubkey;

	uint8_t		 ca_privkey_method;
};

pid_t
caproc(struct privsep *ps, struct privsep_proc *p)
{
	return (proc_run(ps, p, procs, nitems(procs), ca_run, NULL));
}

void
ca_run(struct privsep *ps, struct privsep_proc *p, void *arg)
{
	struct iked	*env = ps->ps_env;
	struct ca_store	*store;

	/*
	 * pledge in the ca process:
	 * stdio - for malloc and basic I/O including events.
	 * rpath - for certificate files.
	 * recvfd - for ocsp sockets.
	 */
	if (pledge("stdio rpath recvfd", NULL) == -1)
		fatal("pledge");

	if ((store = calloc(1, sizeof(*store))) == NULL)
		fatal("%s: failed to allocate cert store", __func__);

	env->sc_priv = store;
	p->p_shutdown = ca_shutdown;
}

void
ca_shutdown(struct privsep_proc *p)
{
	struct iked             *env = p->p_env;
	struct ca_store		*store;

	if (env == NULL)
		return;
	ibuf_release(env->sc_certreq);
	if ((store = env->sc_priv) == NULL)
		return;
	ibuf_release(store->ca_pubkey.id_buf);
	ibuf_release(store->ca_privkey.id_buf);
	free(store);
}

void
ca_getkey(struct privsep *ps, struct iked_id *key, enum imsg_type type)
{
	struct iked	*env = ps->ps_env;
	struct ca_store	*store = env->sc_priv;
	struct iked_id	*id;
	const char	*name;

	if (store == NULL)
		fatalx("%s: invalid store", __func__);

	if (type == IMSG_PRIVKEY) {
		name = "private";
		id = &store->ca_privkey;

		store->ca_privkey_method = ca_privkey_to_method(key);
		if (store->ca_privkey_method == IKEV2_AUTH_NONE)
			fatalx("ca: failed to get auth method for privkey");
	} else if (type == IMSG_PUBKEY) {
		name = "public";
		id = &store->ca_pubkey;
	} else
		fatalx("%s: invalid type %d", __func__, type);

	log_debug("%s: received %s key type %s length %zd", __func__,
	    name, print_map(key->id_type, ikev2_cert_map),
	    ibuf_length(key->id_buf));

	/* clear old key and copy new one */
	ibuf_release(id->id_buf);
	memcpy(id, key, sizeof(*id));
}

void
ca_reset(struct privsep *ps)
{
	struct iked	*env = ps->ps_env;
	struct ca_store	*store = env->sc_priv;

	if (store->ca_privkey.id_type == IKEV2_ID_NONE ||
	    store->ca_pubkey.id_type == IKEV2_ID_NONE)
		fatalx("ca_reset: keys not loaded");

	if (store->ca_cas != NULL)
		X509_STORE_free(store->ca_cas);
	if (store->ca_certs != NULL)
		X509_STORE_free(store->ca_certs);

	if ((store->ca_cas = X509_STORE_new()) == NULL)
		fatalx("ca_reset: failed to get ca store");
	if ((store->ca_calookup = X509_STORE_add_lookup(store->ca_cas,
	    X509_LOOKUP_file())) == NULL)
		fatalx("ca_reset: failed to add ca lookup");

	if ((store->ca_certs = X509_STORE_new()) == NULL)
		fatalx("ca_reset: failed to get cert store");
	if ((store->ca_certlookup = X509_STORE_add_lookup(store->ca_certs,
	    X509_LOOKUP_file())) == NULL)
		fatalx("ca_reset: failed to add cert lookup");

	if (ca_reload(env) != 0)
		fatal("ca_reset: reload");
}

int
ca_dispatch_parent(int fd, struct privsep_proc *p, struct imsg *imsg)
{
	struct iked		*env = p->p_env;
	unsigned int		 mode;

	switch (imsg->hdr.type) {
	case IMSG_CTL_RESET:
		IMSG_SIZE_CHECK(imsg, &mode);
		memcpy(&mode, imsg->data, sizeof(mode));
		if (mode == RESET_ALL || mode == RESET_CA) {
			log_debug("%s: config reset", __func__);
			ca_reset(&env->sc_ps);
		}
		break;
	case IMSG_OCSP_FD:
		ocsp_receive_fd(env, imsg);
		break;
	case IMSG_OCSP_CFG:
		config_getocsp(env, imsg);
		break;
	case IMSG_PRIVKEY:
	case IMSG_PUBKEY:
		config_getkey(env, imsg);
		break;
	case IMSG_CERT_PARTIAL_CHAIN:
		config_getcertpartialchain(env, imsg);
		break;
	default:
		return (-1);
	}

	return (0);
}

int
ca_dispatch_ikev2(int fd, struct privsep_proc *p, struct imsg *imsg)
{
	struct iked	*env = p->p_env;

	switch (imsg->hdr.type) {
	case IMSG_CERTREQ:
		ca_getreq(env, imsg);
		break;
	case IMSG_CERT:
		ca_getcert(env, imsg);
		break;
	case IMSG_AUTH:
		ca_getauth(env, imsg);
		break;
	default:
		return (-1);
	}

	return (0);
}

int
ca_setcert(struct iked *env, struct iked_sahdr *sh, struct iked_id *id,
    uint8_t type, uint8_t *data, size_t len, enum privsep_procid procid)
{
	struct iovec		iov[4];
	int			iovcnt = 0;
	struct iked_static_id	idb;

	/* Must send the cert and a valid Id to the ca process */
	if (procid == PROC_CERT) {
		if (id == NULL || id->id_type == IKEV2_ID_NONE ||
		    ibuf_length(id->id_buf) > IKED_ID_SIZE)
			return (-1);
		bzero(&idb, sizeof(idb));

		/* Convert to a static Id */
		idb.id_type = id->id_type;
		idb.id_offset = id->id_offset;
		idb.id_length = ibuf_length(id->id_buf);
		memcpy(&idb.id_data, ibuf_data(id->id_buf),
		    ibuf_length(id->id_buf));

		iov[iovcnt].iov_base = &idb;
		iov[iovcnt].iov_len = sizeof(idb);
		iovcnt++;
	}

	iov[iovcnt].iov_base = sh;
	iov[iovcnt].iov_len = sizeof(*sh);
	iovcnt++;
	iov[iovcnt].iov_base = &type;
	iov[iovcnt].iov_len = sizeof(type);
	iovcnt++;
	if (data != NULL) {
		iov[iovcnt].iov_base = data;
		iov[iovcnt].iov_len = len;
		iovcnt++;
	}

	if (proc_composev(&env->sc_ps, procid, IMSG_CERT, iov, iovcnt) == -1)
		return (-1);
	return (0);
}

int
ca_setreq(struct iked *env, struct iked_sa *sa,
    struct iked_static_id *localid, uint8_t type, uint8_t more, uint8_t *data,
    size_t len, enum privsep_procid procid)
{
	struct iovec		iov[5];
	int			iovcnt = 0;
	struct iked_static_id	idb;
	struct iked_id		id;
	int			ret = -1;

	/* Convert to a static Id */
	bzero(&id, sizeof(id));
	if (ikev2_policy2id(localid, &id, 1) != 0)
		return (-1);

	if (ibuf_length(id.id_buf) > IKED_ID_SIZE)
		return (-1);
	bzero(&idb, sizeof(idb));
	idb.id_type = id.id_type;
	idb.id_offset = id.id_offset;
	idb.id_length = ibuf_length(id.id_buf);
	memcpy(&idb.id_data, ibuf_data(id.id_buf), ibuf_length(id.id_buf));
	iov[iovcnt].iov_base = &idb;
	iov[iovcnt].iov_len = sizeof(idb);
	iovcnt++;

	iov[iovcnt].iov_base = &sa->sa_hdr;
	iov[iovcnt].iov_len = sizeof(sa->sa_hdr);
	iovcnt++;
	iov[iovcnt].iov_base = &type;
	iov[iovcnt].iov_len = sizeof(type);
	iovcnt++;
	iov[iovcnt].iov_base = &more;
	iov[iovcnt].iov_len = sizeof(more);
	iovcnt++;
	if (data != NULL) {
		iov[iovcnt].iov_base = data;
		iov[iovcnt].iov_len = len;
		iovcnt++;
	}

	if (proc_composev(&env->sc_ps, procid, IMSG_CERTREQ, iov, iovcnt) == -1)
		goto done;

	sa_stateflags(sa, IKED_REQ_CERTREQ);

	ret = 0;
 done:
	ibuf_release(id.id_buf);
	return (ret);
}

static int
auth_sig_compatible(uint8_t type)
{
	switch (type) {
	case IKEV2_AUTH_RSA_SIG:
	case IKEV2_AUTH_ECDSA_256:
	case IKEV2_AUTH_ECDSA_384:
	case IKEV2_AUTH_ECDSA_521:
	case IKEV2_AUTH_SIG_ANY:
		return (1);
	}
	return (0);
}

int
ca_setauth(struct iked *env, struct iked_sa *sa,
    struct ibuf *authmsg, enum privsep_procid id)
{
	struct iovec		 iov[3];
	int			 iovcnt = 3;
	struct iked_policy	*policy = sa->sa_policy;
	uint8_t			 type = policy->pol_auth.auth_method;

	if (id == PROC_CERT) {
		/* switch encoding to IKEV2_AUTH_SIG if SHA2 is supported */
		if (sa->sa_sigsha2 && auth_sig_compatible(type)) {
			log_debug("%s: switching %s to SIG", __func__,
			    print_map(type, ikev2_auth_map));
			type = IKEV2_AUTH_SIG;
		} else if (!sa->sa_sigsha2 && type == IKEV2_AUTH_SIG_ANY) {
			log_debug("%s: switching SIG to RSA_SIG(*)", __func__);
			/* XXX ca might auto-switch to ECDSA */
			type = IKEV2_AUTH_RSA_SIG;
		} else if (type == IKEV2_AUTH_SIG) {
			log_debug("%s: using SIG (RFC7427)", __func__);
		}
	}

	if (type == IKEV2_AUTH_SHARED_KEY_MIC) {
		sa->sa_stateflags |= IKED_REQ_AUTH;
		return (ikev2_msg_authsign(env, sa,
		    &policy->pol_auth, authmsg));
	}

	iov[0].iov_base = &sa->sa_hdr;
	iov[0].iov_len = sizeof(sa->sa_hdr);
	iov[1].iov_base = &type;
	iov[1].iov_len = sizeof(type);
	if (type == IKEV2_AUTH_NONE)
		iovcnt--;
	else {
		iov[2].iov_base = ibuf_data(authmsg);
		iov[2].iov_len = ibuf_size(authmsg);
		log_debug("%s: auth length %zu", __func__, ibuf_size(authmsg));
	}

	if (proc_composev(&env->sc_ps, id, IMSG_AUTH, iov, iovcnt) == -1)
		return (-1);
	return (0);
}

int
ca_getcert(struct iked *env, struct imsg *imsg)
{
	X509			*issuer = NULL;
	struct iked_sahdr	 sh;
	uint8_t			 type;
	uint8_t			*ptr;
	size_t			 len;
	struct iked_static_id	 id;
	unsigned int		 i;
	struct iovec		 iov[3];
	int			 iovcnt = 3, cmd, ret = 0;
	struct iked_id		 key;

	ptr = (uint8_t *)imsg->data;
	len = IMSG_DATA_SIZE(imsg);
	i = sizeof(id) + sizeof(sh) + sizeof(type);
	if (len < i)
		return (-1);

	memcpy(&id, ptr, sizeof(id));
	if (id.id_type == IKEV2_ID_NONE)
		return (-1);
	memcpy(&sh, ptr + sizeof(id), sizeof(sh));
	memcpy(&type, ptr + sizeof(id) + sizeof(sh), sizeof(uint8_t));

	ptr += i;
	len -= i;

	bzero(&key, sizeof(key));

	switch (type) {
	case IKEV2_CERT_X509_CERT:
		if (env->sc_ocsp_url == NULL)
			ret = ca_validate_cert(env, &id, ptr, len, NULL);
		else {
			ret = ca_validate_cert(env, &id, ptr, len, &issuer);
			if (ret == 0) {
				ret = ocsp_validate_cert(env, ptr, len, sh,
				    type, issuer);
				X509_free(issuer);
				if (ret == 0)
					return (0);
			} else
				X509_free(issuer);
		}
		break;
	case IKEV2_CERT_RSA_KEY:
	case IKEV2_CERT_ECDSA:
		ret = ca_validate_pubkey(env, &id, ptr, len, NULL);
		break;
	case IKEV2_CERT_NONE:
		/* Fallback to public key */
		ret = ca_validate_pubkey(env, &id, NULL, 0, &key);
		if (ret == 0) {
			ptr = ibuf_data(key.id_buf);
			len = ibuf_length(key.id_buf);
			type = key.id_type;
		}
		break;
	default:
		log_debug("%s: unsupported cert type %d", __func__, type);
		ret = -1;
		break;
	}

	if (ret == 0)
		cmd = IMSG_CERTVALID;
	else
		cmd = IMSG_CERTINVALID;

	iov[0].iov_base = &sh;
	iov[0].iov_len = sizeof(sh);
	iov[1].iov_base = &type;
	iov[1].iov_len = sizeof(type);
	iov[2].iov_base = ptr;
	iov[2].iov_len = len;

	if (proc_composev(&env->sc_ps, PROC_IKEV2, cmd, iov, iovcnt) == -1)
		return (-1);
	return (0);
}

int
ca_getreq(struct iked *env, struct imsg *imsg)
{
	struct ca_store		*store = env->sc_priv;
	struct iked_sahdr	 sh;
	uint8_t			 type, more;
	uint8_t			*ptr;
	size_t			 len;
	unsigned int		 i;
	X509			*ca = NULL, *cert = NULL;
	struct ibuf		*buf;
	struct iked_static_id	 id;
	char			 idstr[IKED_ID_SIZE];
	X509_NAME		*subj;
	char			*subj_name;

	ptr = (uint8_t *)imsg->data;
	len = IMSG_DATA_SIZE(imsg);
	i = sizeof(id) + sizeof(type) + sizeof(sh) + sizeof(more);
	if (len < i)
		return (-1);

	memcpy(&id, ptr, sizeof(id));
	if (id.id_type == IKEV2_ID_NONE)
		return (-1);
	memcpy(&sh, ptr + sizeof(id), sizeof(sh));
	memcpy(&type, ptr + sizeof(id) + sizeof(sh), sizeof(type));
	memcpy(&more, ptr + sizeof(id) + sizeof(sh) + sizeof(type), sizeof(more));

	ptr += i;
	len -= i;

	switch (type) {
	case IKEV2_CERT_RSA_KEY:
	case IKEV2_CERT_ECDSA:
		/*
		 * Find a local raw public key that matches the type
		 * received in the CERTREQ payoad
		 */
		if (store->ca_pubkey.id_type != type ||
		    store->ca_pubkey.id_buf == NULL)
			goto fallback;

		buf = ibuf_dup(store->ca_pubkey.id_buf);
		log_debug("%s: using local public key of type %s", __func__,
		    print_map(type, ikev2_cert_map));
		break;
	case IKEV2_CERT_X509_CERT:
		if (len == 0 || len % SHA_DIGEST_LENGTH) {
			log_info("%s: invalid CERTREQ data.",
			    SPI_SH(&sh, __func__));
			return (-1);
		}

		/*
		 * Find a local certificate signed by any of the CAs
		 * received in the CERTREQ payload
		 */
		for (i = 0; i < len; i += SHA_DIGEST_LENGTH) {
			if ((ca = ca_by_subjectpubkey(store->ca_cas, ptr + i,
			    SHA_DIGEST_LENGTH)) == NULL)
				continue;
			subj = X509_get_subject_name(ca);
			if (subj == NULL)
				return (-1);
			subj_name = X509_NAME_oneline(subj, NULL, 0);
			log_debug("%s: found CA %s", __func__, subj_name);

			if ((cert = ca_by_issuer(store->ca_certs,
			    subj, &id)) != NULL) {
				/* XXX
				 * should we re-validate our own cert here?
				 */
				break;
			}
		}
		/* Fallthrough */
	case IKEV2_CERT_NONE:
 fallback:
		/*
		 * If no certificate or key matching any of the trust-anchors
		 * was found and this was the last CERTREQ, try to find one with
		 * subjectAltName matching the ID
		 */
		if (cert == NULL && more)
			return (0);

		if (cert == NULL)
			cert = ca_by_subjectaltname(store->ca_certs, &id);

		/* Set type if coming from fallback */
		if (cert != NULL)
			type = IKEV2_CERT_X509_CERT;

		/* If there is no matching certificate use local raw pubkey */
		if (cert == NULL) {
			if (ikev2_print_static_id(&id, idstr, sizeof(idstr)) == -1)
				return (-1);
			log_info("%s: no valid local certificate found for %s",
			    SPI_SH(&sh, __func__), idstr);
			ca_store_certs_info(SPI_SH(&sh, __func__),
			    store->ca_certs);
			if (store->ca_pubkey.id_buf == NULL)
				return (-1);
			buf = ibuf_dup(store->ca_pubkey.id_buf);
			type = store->ca_pubkey.id_type;
			log_info("%s: using local public key of type %s",
			    SPI_SH(&sh, __func__),
			    print_map(type, ikev2_cert_map));
			break;
		}

		subj = X509_get_subject_name(cert);
		if (subj == NULL)
			return (-1);
		subj_name = X509_NAME_oneline(subj, NULL, 0);
		log_debug("%s: found local certificate %s", __func__,
		    subj_name);

		if ((buf = ca_x509_serialize(cert)) == NULL)
			return (-1);
		break;
	default:
		log_warnx("%s: unknown cert type requested",
		    SPI_SH(&sh, __func__));
		return (-1);
	}

	ca_setcert(env, &sh, NULL, type,
	    ibuf_data(buf), ibuf_size(buf), PROC_IKEV2);
	ibuf_release(buf);

	return (0);
}

int
ca_getauth(struct iked *env, struct imsg *imsg)
{
	struct ca_store		*store = env->sc_priv;
	struct iked_sahdr	 sh;
	uint8_t			 method;
	uint8_t			*ptr;
	size_t			 len;
	unsigned int		 i;
	int			 ret = -1;
	struct iked_sa		 sa;
	struct iked_policy	 policy;
	struct iked_id		*id;
	struct ibuf		*authmsg;

	ptr = (uint8_t *)imsg->data;
	len = IMSG_DATA_SIZE(imsg);
	i = sizeof(method) + sizeof(sh);
	if (len <= i)
		return (-1);

	memcpy(&sh, ptr, sizeof(sh));
	memcpy(&method, ptr + sizeof(sh), sizeof(uint8_t));
	if (method == IKEV2_AUTH_SHARED_KEY_MIC)
		return (-1);

	ptr += i;
	len -= i;

	if ((authmsg = ibuf_new(ptr, len)) == NULL)
		return (-1);

	/*
	 * Create fake SA and policy
	 */
	bzero(&sa, sizeof(sa));
	bzero(&policy, sizeof(policy));
	memcpy(&sa.sa_hdr, &sh, sizeof(sh));
	sa.sa_policy = &policy;
	if (sh.sh_initiator)
		id = &sa.sa_icert;
	else
		id = &sa.sa_rcert;
	memcpy(id, &store->ca_privkey, sizeof(*id));
	policy.pol_auth.auth_method = method == IKEV2_AUTH_SIG ?
	    method : store->ca_privkey_method;

	if (ikev2_msg_authsign(env, &sa, &policy.pol_auth, authmsg) != 0) {
		log_debug("%s: AUTH sign failed", __func__);
		policy.pol_auth.auth_method = IKEV2_AUTH_NONE;
	}

	ret = ca_setauth(env, &sa, sa.sa_localauth.id_buf, PROC_IKEV2);

	ibuf_release(sa.sa_localauth.id_buf);
	sa.sa_localauth.id_buf = NULL;
	ibuf_release(authmsg);

	return (ret);
}

int
ca_reload(struct iked *env)
{
	struct ca_store		*store = env->sc_priv;
	uint8_t			 md[EVP_MAX_MD_SIZE];
	char			 file[PATH_MAX];
	struct iovec		 iov[2];
	struct dirent		*entry;
	STACK_OF(X509_OBJECT)	*h;
	X509_OBJECT		*xo;
	X509			*x509;
	DIR			*dir;
	int			 i, iovcnt = 0;
	unsigned int		 len;
	X509_NAME		*subj;
	char			*subj_name;

	/*
	 * Load CAs
	 */
	if ((dir = opendir(IKED_CA_DIR)) == NULL)
		return (-1);

	while ((entry = readdir(dir)) != NULL) {
		if ((entry->d_type != DT_REG) &&
		    (entry->d_type != DT_LNK))
			continue;

		if (snprintf(file, sizeof(file), "%s%s",
		    IKED_CA_DIR, entry->d_name) < 0)
			continue;

		if (!X509_load_cert_file(store->ca_calookup, file,
		    X509_FILETYPE_PEM)) {
			log_warn("%s: failed to load ca file %s", __func__,
			    entry->d_name);
			ca_sslerror(__func__);
			continue;
		}
		log_debug("%s: loaded ca file %s", __func__, entry->d_name);
	}
	closedir(dir);

	/*
	 * Load CRLs for the CAs
	 */
	if ((dir = opendir(IKED_CRL_DIR)) == NULL)
		return (-1);

	while ((entry = readdir(dir)) != NULL) {
		if ((entry->d_type != DT_REG) &&
		    (entry->d_type != DT_LNK))
			continue;

		if (snprintf(file, sizeof(file), "%s%s",
		    IKED_CRL_DIR, entry->d_name) < 0)
			continue;

		if (!X509_load_crl_file(store->ca_calookup, file,
		    X509_FILETYPE_PEM)) {
			log_warn("%s: failed to load crl file %s", __func__,
			    entry->d_name);
			ca_sslerror(__func__);
			continue;
		}

		/* Only enable CRL checks if we actually loaded a CRL */
		X509_STORE_set_flags(store->ca_cas, X509_V_FLAG_CRL_CHECK);

		log_debug("%s: loaded crl file %s", __func__, entry->d_name);
	}
	closedir(dir);

	/*
	 * Save CAs signatures for the IKEv2 CERTREQ
	 */
	ibuf_release(env->sc_certreq);
	if ((env->sc_certreq = ibuf_new(NULL, 0)) == NULL)
		return (-1);

	h = X509_STORE_get0_objects(store->ca_cas);
	for (i = 0; i < sk_X509_OBJECT_num(h); i++) {
		xo = sk_X509_OBJECT_value(h, i);
		if (X509_OBJECT_get_type(xo) != X509_LU_X509)
			continue;

		x509 = X509_OBJECT_get0_X509(xo);
		len = sizeof(md);
		ca_subjectpubkey_digest(x509, md, &len);
		subj = X509_get_subject_name(x509);
		if (subj == NULL)
			return (-1);
		subj_name = X509_NAME_oneline(subj, NULL, 0);
		log_debug("%s: %s", __func__, subj_name);

		if (ibuf_add(env->sc_certreq, md, len) != 0) {
			ibuf_release(env->sc_certreq);
			env->sc_certreq = NULL;
			return (-1);
		}
	}

	if (ibuf_length(env->sc_certreq)) {
		env->sc_certreqtype = IKEV2_CERT_X509_CERT;
		iov[0].iov_base = &env->sc_certreqtype;
		iov[0].iov_len = sizeof(env->sc_certreqtype);
		iovcnt++;
		iov[1].iov_base = ibuf_data(env->sc_certreq);
		iov[1].iov_len = ibuf_length(env->sc_certreq);
		iovcnt++;

		log_debug("%s: loaded %zu ca certificate%s", __func__,
		    ibuf_length(env->sc_certreq) / SHA_DIGEST_LENGTH,
		    ibuf_length(env->sc_certreq) == SHA_DIGEST_LENGTH ?
		    "" : "s");

		(void)proc_composev(&env->sc_ps, PROC_IKEV2, IMSG_CERTREQ,
		    iov, iovcnt);
	}

	/*
	 * Load certificates
	 */
	if ((dir = opendir(IKED_CERT_DIR)) == NULL)
		return (-1);

	while ((entry = readdir(dir)) != NULL) {
		if ((entry->d_type != DT_REG) &&
		    (entry->d_type != DT_LNK))
			continue;

		if (snprintf(file, sizeof(file), "%s%s",
		    IKED_CERT_DIR, entry->d_name) < 0)
			continue;

		if (!X509_load_cert_file(store->ca_certlookup, file,
		    X509_FILETYPE_PEM)) {
			log_warn("%s: failed to load cert file %s", __func__,
			    entry->d_name);
			ca_sslerror(__func__);
			continue;
		}
		log_debug("%s: loaded cert file %s", __func__, entry->d_name);
	}
	closedir(dir);

	h = X509_STORE_get0_objects(store->ca_certs);
	for (i = 0; i < sk_X509_OBJECT_num(h); i++) {
		xo = sk_X509_OBJECT_value(h, i);
		if (X509_OBJECT_get_type(xo) != X509_LU_X509)
			continue;

		x509 = X509_OBJECT_get0_X509(xo);

		(void)ca_validate_cert(env, NULL, x509, 0, NULL);
	}

	if (!env->sc_certreqtype)
		env->sc_certreqtype = store->ca_pubkey.id_type;

	log_debug("%s: local cert type %s", __func__,
	    print_map(env->sc_certreqtype, ikev2_cert_map));

	iov[0].iov_base = &env->sc_certreqtype;
	iov[0].iov_len = sizeof(env->sc_certreqtype);
	if (iovcnt == 0)
		iovcnt++;
	(void)proc_composev(&env->sc_ps, PROC_IKEV2, IMSG_CERTREQ, iov, iovcnt);

	return (0);
}

X509 *
ca_by_subjectpubkey(X509_STORE *ctx, uint8_t *sig, size_t siglen)
{
	STACK_OF(X509_OBJECT)	*h;
	X509_OBJECT		*xo;
	X509			*ca;
	int			 i;
	unsigned int		 len;
	uint8_t			 md[EVP_MAX_MD_SIZE];

	h = X509_STORE_get0_objects(ctx);

	for (i = 0; i < sk_X509_OBJECT_num(h); i++) {
		xo = sk_X509_OBJECT_value(h, i);
		if (X509_OBJECT_get_type(xo) != X509_LU_X509)
			continue;

		ca = X509_OBJECT_get0_X509(xo);
		len = sizeof(md);
		ca_subjectpubkey_digest(ca, md, &len);

		if (len == siglen && memcmp(md, sig, len) == 0)
			return (ca);
	}

	return (NULL);
}

X509 *
ca_by_issuer(X509_STORE *ctx, X509_NAME *subject, struct iked_static_id *id)
{
	STACK_OF(X509_OBJECT)	*h;
	X509_OBJECT		*xo;
	X509			*cert;
	int			 i;
	X509_NAME		*issuer;

	if (subject == NULL)
		return (NULL);

	h = X509_STORE_get0_objects(ctx);
	for (i = 0; i < sk_X509_OBJECT_num(h); i++) {
		xo = sk_X509_OBJECT_value(h, i);
		if (X509_OBJECT_get_type(xo) != X509_LU_X509)
			continue;

		cert = X509_OBJECT_get0_X509(xo);
		if ((issuer = X509_get_issuer_name(cert)) == NULL)
			continue;
		else if (X509_NAME_cmp(subject, issuer) == 0) {
			switch (id->id_type) {
			case IKEV2_ID_ASN1_DN:
				if (ca_x509_subject_cmp(cert, id) == 0)
					return (cert);
				break;
			default:
				if (ca_x509_subjectaltname_cmp(cert, id) == 0)
					return (cert);
				break;
			}
		}
	}

	return (NULL);
}

X509 *
ca_by_subjectaltname(X509_STORE *ctx, struct iked_static_id *id)
{
	STACK_OF(X509_OBJECT)	*h;
	X509_OBJECT		*xo;
	X509			*cert;
	int			 i;

	h = X509_STORE_get0_objects(ctx);
	for (i = 0; i < sk_X509_OBJECT_num(h); i++) {
		xo = sk_X509_OBJECT_value(h, i);
		if (X509_OBJECT_get_type(xo) != X509_LU_X509)
			continue;

		cert = X509_OBJECT_get0_X509(xo);
		switch (id->id_type) {
		case IKEV2_ID_ASN1_DN:
			if (ca_x509_subject_cmp(cert, id) == 0)
				return (cert);
			break;
		default:
			if (ca_x509_subjectaltname_cmp(cert, id) == 0)
				return (cert);
			break;
		}
	}

	return (NULL);
}

void
ca_store_certs_info(const char *msg, X509_STORE *ctx)
{
	STACK_OF(X509_OBJECT)	*h;
	X509_OBJECT		*xo;
	X509			*cert;
	int			 i;

	h = X509_STORE_get0_objects(ctx);
	for (i = 0; i < sk_X509_OBJECT_num(h); i++) {
		xo = sk_X509_OBJECT_value(h, i);
		if (X509_OBJECT_get_type(xo) != X509_LU_X509)
			continue;
		cert = X509_OBJECT_get0_X509(xo);
		ca_cert_info(msg, cert);
	}
}

void
ca_cert_info(const char *msg, X509 *cert)
{
	ASN1_INTEGER	*asn1_serial;
	BUF_MEM		*memptr;
	BIO		*rawserial = NULL;
	char		 buf[BUFSIZ];
	X509_NAME	*issuer;

	if ((asn1_serial = X509_get_serialNumber(cert)) == NULL ||
	    (rawserial = BIO_new(BIO_s_mem())) == NULL ||
	    i2a_ASN1_INTEGER(rawserial, asn1_serial) <= 0)
		goto out;

	issuer = X509_get_issuer_name(cert);
	if (issuer == NULL)
		goto out;
	if (X509_NAME_oneline(issuer, buf, sizeof(buf)))
		log_info("%s: issuer: %s", msg, buf);
	BIO_get_mem_ptr(rawserial, &memptr);
	if (memptr->data != NULL && memptr->length < INT32_MAX)
		log_info("%s: serial: %.*s", msg, (int)memptr->length,
		    memptr->data);
	if (X509_NAME_oneline(issuer, buf, sizeof(buf)))
		log_info("%s: subject: %s", msg, buf);
	ca_x509_subjectaltname_log(cert, msg);
out:
	if (rawserial)
		BIO_free(rawserial);
}

int
ca_subjectpubkey_digest(X509 *x509, uint8_t *md, unsigned int *size)
{
	EVP_PKEY	*pkey;
	uint8_t		*buf = NULL;
	int		 buflen;

	if (*size < SHA_DIGEST_LENGTH)
		return (-1);

	/*
	 * Generate a SHA-1 digest of the Subject Public Key Info
	 * element in the X.509 certificate, an ASN.1 sequence
	 * that includes the public key type (eg. RSA) and the
	 * public key value (see 3.7 of RFC7296).
	 */
	if ((pkey = X509_get_pubkey(x509)) == NULL)
		return (-1);
	buflen = i2d_PUBKEY(pkey, &buf);
	EVP_PKEY_free(pkey);
	if (buflen == 0)
		return (-1);
	if (!EVP_Digest(buf, buflen, md, size, EVP_sha1(), NULL)) {
		free(buf);
		return (-1);
	}
	free(buf);

	return (0);
}

struct ibuf *
ca_x509_serialize(X509 *x509)
{
	long		 len;
	struct ibuf	*buf;
	uint8_t		*d = NULL;
	BIO		*out;

	if ((out = BIO_new(BIO_s_mem())) == NULL)
		return (NULL);
	if (!i2d_X509_bio(out, x509)) {
		BIO_free(out);
		return (NULL);
	}

	len = BIO_get_mem_data(out, &d);
	buf = ibuf_new(d, len);
	BIO_free(out);

	return (buf);
}

int
ca_pubkey_serialize(EVP_PKEY *key, struct iked_id *id)
{
	RSA		*rsa = NULL;
	EC_KEY		*ec = NULL;
	uint8_t		*d;
	int		 len = 0;
	int		 ret = -1;

	switch (EVP_PKEY_id(key)) {
	case EVP_PKEY_RSA:
		id->id_type = 0;
		id->id_offset = 0;
		ibuf_release(id->id_buf);
		id->id_buf = NULL;

		if ((rsa = EVP_PKEY_get1_RSA(key)) == NULL)
			goto done;
		if ((len = i2d_RSAPublicKey(rsa, NULL)) <= 0)
			goto done;
		if ((id->id_buf = ibuf_new(NULL, len)) == NULL)
			goto done;

		d = ibuf_data(id->id_buf);
		if (i2d_RSAPublicKey(rsa, &d) != len) {
			ibuf_release(id->id_buf);
			id->id_buf = NULL;
			goto done;
		}

		id->id_type = IKEV2_CERT_RSA_KEY;
		break;
	case EVP_PKEY_EC:
		id->id_type = 0;
		id->id_offset = 0;
		ibuf_release(id->id_buf);
		id->id_buf = NULL;

		if ((ec = EVP_PKEY_get1_EC_KEY(key)) == NULL)
			goto done;
		if ((len = i2d_EC_PUBKEY(ec, NULL)) <= 0)
			goto done;
		if ((id->id_buf = ibuf_new(NULL, len)) == NULL)
			goto done;

		d = ibuf_data(id->id_buf);
		if (i2d_EC_PUBKEY(ec, &d) != len) {
			ibuf_release(id->id_buf);
			id->id_buf = NULL;
			goto done;
		}

		id->id_type = IKEV2_CERT_ECDSA;
		break;
	default:
		log_debug("%s: unsupported key type %d", __func__,
		    EVP_PKEY_id(key));
		return (-1);
	}

	log_debug("%s: type %s length %d", __func__,
	    print_map(id->id_type, ikev2_cert_map), len);

	ret = 0;
 done:
	if (rsa != NULL)
		RSA_free(rsa);
	if (ec != NULL)
		EC_KEY_free(ec);
	return (ret);
}

int
ca_privkey_serialize(EVP_PKEY *key, struct iked_id *id)
{
	RSA		*rsa = NULL;
	EC_KEY		*ec = NULL;
	uint8_t		*d;
	int		 len = 0;
	int		 ret = -1;

	switch (EVP_PKEY_id(key)) {
	case EVP_PKEY_RSA:
		id->id_type = 0;
		id->id_offset = 0;
		ibuf_release(id->id_buf);
		id->id_buf = NULL;

		if ((rsa = EVP_PKEY_get1_RSA(key)) == NULL)
			goto done;
		if ((len = i2d_RSAPrivateKey(rsa, NULL)) <= 0)
			goto done;
		if ((id->id_buf = ibuf_new(NULL, len)) == NULL)
			goto done;

		d = ibuf_data(id->id_buf);
		if (i2d_RSAPrivateKey(rsa, &d) != len) {
			ibuf_release(id->id_buf);
			id->id_buf = NULL;
			goto done;
		}

		id->id_type = IKEV2_CERT_RSA_KEY;
		break;
	case EVP_PKEY_EC:
		id->id_type = 0;
		id->id_offset = 0;
		ibuf_release(id->id_buf);
		id->id_buf = NULL;

		if ((ec = EVP_PKEY_get1_EC_KEY(key)) == NULL)
			goto done;
		if ((len = i2d_ECPrivateKey(ec, NULL)) <= 0)
			goto done;
		if ((id->id_buf = ibuf_new(NULL, len)) == NULL)
			goto done;

		d = ibuf_data(id->id_buf);
		if (i2d_ECPrivateKey(ec, &d) != len) {
			ibuf_release(id->id_buf);
			id->id_buf = NULL;
			goto done;
		}

		id->id_type = IKEV2_CERT_ECDSA;
		break;
	default:
		log_debug("%s: unsupported key type %d", __func__,
		    EVP_PKEY_id(key));
		return (-1);
	}

	log_debug("%s: type %s length %d", __func__,
	    print_map(id->id_type, ikev2_cert_map), len);

	ret = 0;
 done:
	if (rsa != NULL)
		RSA_free(rsa);
	if (ec != NULL)
		EC_KEY_free(ec);
	return (ret);
}

int
ca_privkey_to_method(struct iked_id *privkey)
{
	BIO		*rawcert = NULL;
	EC_KEY		*ec = NULL;
	const EC_GROUP	*group = NULL;
	uint8_t	 method = IKEV2_AUTH_NONE;

	switch (privkey->id_type) {
	case IKEV2_CERT_RSA_KEY:
		method = IKEV2_AUTH_RSA_SIG;
		break;
	case IKEV2_CERT_ECDSA:
		if ((rawcert = BIO_new_mem_buf(ibuf_data(privkey->id_buf),
		    ibuf_length(privkey->id_buf))) == NULL)
			goto out;
		if ((ec = d2i_ECPrivateKey_bio(rawcert, NULL)) == NULL)
			goto out;
		if ((group = EC_KEY_get0_group(ec)) == NULL)
			goto out;
		switch (EC_GROUP_get_degree(group)) {
		case 256:
			method = IKEV2_AUTH_ECDSA_256;
			break;
		case 384:
			method = IKEV2_AUTH_ECDSA_384;
			break;
		case 521:
			method = IKEV2_AUTH_ECDSA_521;
			break;
		}
	}

	log_debug("%s: type %s method %s", __func__,
	    print_map(privkey->id_type, ikev2_cert_map),
	    print_map(method, ikev2_auth_map));

 out:
	if (ec != NULL)
		EC_KEY_free(ec);
	if (rawcert != NULL)
		BIO_free(rawcert);

	return (method);
}

char *
ca_asn1_name(uint8_t *asn1, size_t len)
{
	X509_NAME	*name = NULL;
	char		*str = NULL;
	const uint8_t	*p;

	p = asn1;
	if ((name = d2i_X509_NAME(NULL, &p, len)) == NULL)
		return (NULL);
	str = X509_NAME_oneline(name, NULL, 0);
	X509_NAME_free(name);

	return (str);
}

/*
 * Copy 'src' to 'dst' until 'marker' is found while unescaping '\'
 * characters. The return value tells the caller where to continue
 * parsing (might be the end of the string) or NULL on error.
 */
static char *
ca_x509_name_unescape(char *src, char *dst, char marker)
{
	while (*src) {
		if (*src == marker) {
			src++;
			break;
		}
		if (*src == '\\') {
			src++;
			if (!*src) {
				log_warnx("%s: '\\' at end of string",
				    __func__);
				*dst = '\0';
				return (NULL);
			}
		}
		*dst++ = *src++;
	}
	*dst = '\0';
	return (src);
}
/*
 * Parse an X509 subject name where 'subject' is in the format
 *    /type0=value0/type1=value1/type2=...
 * where characters may be escaped by '\'.
 * See lib/libssl/src/apps/apps.c:parse_name()
 */
void *
ca_x509_name_parse(char *subject)
{
	char		*cp, *value = NULL, *type = NULL;
	size_t		 maxlen;
	X509_NAME	*name = NULL;

	if (*subject != '/') {
		log_warnx("%s: leading '/' missing in '%s'", __func__, subject);
		goto err;
	}

	/* length of subject is upper bound for unescaped type/value */
	maxlen = strlen(subject) + 1;

	if ((type = calloc(1, maxlen)) == NULL ||
	    (value = calloc(1, maxlen)) == NULL ||
	    (name = X509_NAME_new()) == NULL)
		goto err;

	cp = subject + 1;
	while (*cp) {
		/* unescape type, terminated by '=' */
		cp = ca_x509_name_unescape(cp, type, '=');
		if (cp == NULL) {
			log_warnx("%s: could not parse type", __func__);
			goto err;
		}
		if (!*cp) {
			log_warnx("%s: missing value", __func__);
			goto err;
		}
		/* unescape value, terminated by '/' */
		cp = ca_x509_name_unescape(cp, value, '/');
		if (cp == NULL) {
			log_warnx("%s: could not parse value", __func__);
			goto err;
		}
		if (!*type || !*value) {
			log_warnx("%s: empty type or value", __func__);
			goto err;
		}
		log_debug("%s: setting '%s' to '%s'", __func__, type, value);
		if (!X509_NAME_add_entry_by_txt(name, type, MBSTRING_ASC,
		    value, -1, -1, 0)) {
			log_warnx("%s: setting '%s' to '%s' failed", __func__,
			    type, value);
			ca_sslerror(__func__);
			goto err;
		}
	}
	free(type);
	free(value);
	return (name);

err:
	X509_NAME_free(name);
	free(type);
	free(value);
	return (NULL);
}

int
ca_validate_pubkey(struct iked *env, struct iked_static_id *id,
    void *data, size_t len, struct iked_id *out)
{
	BIO		*rawcert = NULL;
	RSA		*peerrsa = NULL, *localrsa = NULL;
	EC_KEY		*peerec = NULL;
	EVP_PKEY	*peerkey = NULL, *localkey = NULL;
	int		 ret = -1;
	FILE		*fp = NULL;
	char		 idstr[IKED_ID_SIZE];
	char		 file[PATH_MAX];
	struct iked_id	 idp;

	switch (id->id_type) {
	case IKEV2_ID_IPV4:
	case IKEV2_ID_FQDN:
	case IKEV2_ID_UFQDN:
	case IKEV2_ID_IPV6:
		break;
	default:
		/* Some types like ASN1_DN will not be mapped to file names */
		log_debug("%s: unsupported public key type %s",
		    __func__, print_map(id->id_type, ikev2_id_map));
		return (-1);
	}

	bzero(&idp, sizeof(idp));
	if ((idp.id_buf = ibuf_new(id->id_data, id->id_length)) == NULL)
		goto done;

	idp.id_type = id->id_type;
	idp.id_offset = id->id_offset;
	if (ikev2_print_id(&idp, idstr, sizeof(idstr)) == -1)
		goto done;

	if (len == 0 && data) {
		/* Data is already an public key */
		peerkey = (EVP_PKEY *)data;
	}
	if (len > 0) {
		if ((rawcert = BIO_new_mem_buf(data, len)) == NULL)
			goto done;

		if ((peerkey = EVP_PKEY_new()) == NULL)
			goto sslerr;
		if ((peerrsa = d2i_RSAPublicKey_bio(rawcert, NULL))) {
			if (!EVP_PKEY_set1_RSA(peerkey, peerrsa)) {
				goto sslerr;
			}
		} else if (BIO_reset(rawcert) == 1 &&
		    (peerec = d2i_EC_PUBKEY_bio(rawcert, NULL))) {
			if (!EVP_PKEY_set1_EC_KEY(peerkey, peerec)) {
				goto sslerr;
			}
		} else {
			log_debug("%s: unknown key type received", __func__);
			goto sslerr;
		}
	}

	lc_idtype(idstr);
	if (strlcpy(file, IKED_PUBKEY_DIR, sizeof(file)) >= sizeof(file) ||
	    strlcat(file, idstr, sizeof(file)) >= sizeof(file)) {
		log_debug("%s: public key id too long %s", __func__, idstr);
		goto done;
	}

	if ((fp = fopen(file, "r")) == NULL) {
		/* Log to debug when called from ca_validate_cert */
		logit(len == 0 ? LOG_DEBUG : LOG_INFO,
		    "%s: could not open public key %s", __func__, file);
		goto done;
	}
	localkey = PEM_read_PUBKEY(fp, NULL, NULL, NULL);
	if (localkey == NULL) {
		/* reading PKCS #8 failed, try PEM RSA */
		rewind(fp);
		localrsa = PEM_read_RSAPublicKey(fp, NULL, NULL, NULL);
		fclose(fp);
		if (localrsa == NULL)
			goto sslerr;
		if ((localkey = EVP_PKEY_new()) == NULL)
			goto sslerr;
		if (!EVP_PKEY_set1_RSA(localkey, localrsa))
			goto sslerr;
	} else {
		fclose(fp);
	}
	if (localkey == NULL)
		goto sslerr;

	if (peerkey && EVP_PKEY_cmp(peerkey, localkey) != 1) {
		log_debug("%s: public key does not match %s", __func__, file);
		goto done;
	}

	log_debug("%s: valid public key in file %s", __func__, file);

	if (out && ca_pubkey_serialize(localkey, out))
		goto done;

	ret = 0;
 sslerr:
	if (ret != 0)
		ca_sslerror(__func__);
 done:
	ibuf_release(idp.id_buf);
	if (localkey != NULL)
		EVP_PKEY_free(localkey);
	if (peerrsa != NULL)
		RSA_free(peerrsa);
	if (peerec != NULL)
		EC_KEY_free(peerec);
	if (localrsa != NULL)
		RSA_free(localrsa);
	if (rawcert != NULL) {
		BIO_free(rawcert);
		if (peerkey != NULL)
			EVP_PKEY_free(peerkey);
	}

	return (ret);
}

int
ca_validate_cert(struct iked *env, struct iked_static_id *id,
    void *data, size_t len, X509 **issuerp)
{
	struct ca_store		*store = env->sc_priv;
	X509_STORE_CTX		*csc = NULL;
	X509_VERIFY_PARAM	*param;
	BIO			*rawcert = NULL;
	X509			*cert = NULL;
	EVP_PKEY		*pkey;
	int			 ret = -1, result, error;
	const char		*errstr = "failed";
	X509_NAME		*subj;
	char			*subj_name;

	if (issuerp)
		*issuerp = NULL;
	if (len == 0) {
		/* Data is already an X509 certificate */
		cert = (X509 *)data;
	} else {
		/* Convert data to X509 certificate */
		if ((rawcert = BIO_new_mem_buf(data, len)) == NULL)
			goto done;
		if ((cert = d2i_X509_bio(rawcert, NULL)) == NULL)
			goto done;
	}

	/* Certificate needs a valid subjectName */
	if (X509_get_subject_name(cert) == NULL) {
		errstr = "invalid subject";
		goto done;
	}

	if (id != NULL) {
		if ((pkey = X509_get_pubkey(cert)) == NULL) {
			errstr = "no public key in cert";
			goto done;
		}
		ret = ca_validate_pubkey(env, id, pkey, 0, NULL);
		EVP_PKEY_free(pkey);
		if (ret == 0) {
			errstr = "in public key file, ok";
			goto done;
		}

		switch (id->id_type) {
		case IKEV2_ID_ASN1_DN:
			if (ca_x509_subject_cmp(cert, id) < 0) {
				errstr = "ASN1_DN identifier mismatch";
				goto done;
			}
			break;
		default:
			if (ca_x509_subjectaltname_cmp(cert, id) != 0) {
				errstr = "invalid subjectAltName extension";
				goto done;
			}
			break;
		}
	}

	csc = X509_STORE_CTX_new();
	if (csc == NULL) {
		errstr = "failed to alloc csc";
		goto done;
	}
	X509_STORE_CTX_init(csc, store->ca_cas, cert, NULL);
	param = X509_STORE_get0_param(store->ca_cas);
	if (X509_VERIFY_PARAM_get_flags(param) & X509_V_FLAG_CRL_CHECK) {
		X509_STORE_CTX_set_flags(csc, X509_V_FLAG_CRL_CHECK);
		X509_STORE_CTX_set_flags(csc, X509_V_FLAG_CRL_CHECK_ALL);
	}
	if (env->sc_cert_partial_chain)
		X509_STORE_CTX_set_flags(csc, X509_V_FLAG_PARTIAL_CHAIN);

	result = X509_verify_cert(csc);
	error = X509_STORE_CTX_get_error(csc);
	if (error == 0 && issuerp) {
		if (X509_STORE_CTX_get1_issuer(issuerp, csc, cert) != 1) {
			log_debug("%s: cannot get issuer", __func__);
			*issuerp = NULL;
		}
	}
	X509_STORE_CTX_cleanup(csc);
	if (error != 0) {
		errstr = X509_verify_cert_error_string(error);
		goto done;
	}

	if (!result) {
		/* XXX should we accept self-signed certificates? */
		errstr = "rejecting self-signed certificate";
		goto done;
	}

	/* Success */
	ret = 0;
	errstr = "ok";

 done:
	if (cert != NULL) {
		subj = X509_get_subject_name(cert);
		if (subj == NULL)
			goto err;
		subj_name = X509_NAME_oneline(subj, NULL, 0);
		log_debug("%s: %s %.100s", __func__, subj_name, errstr);
	}
 err:

	if (rawcert != NULL) {
		BIO_free(rawcert);
		if (cert != NULL)
			X509_free(cert);
	}

	if (csc != NULL)
		X509_STORE_CTX_free(csc);

	return (ret);
}

/* check if subject from cert matches the id */
int
ca_x509_subject_cmp(X509 *cert, struct iked_static_id *id)
{
	X509_NAME	*subject, *idname = NULL;
	const uint8_t	*idptr;
	size_t		 idlen;
	int		 ret = -1;

	if (id->id_type != IKEV2_ID_ASN1_DN)
		return (-1);
	if ((subject = X509_get_subject_name(cert)) == NULL)
		return (-1);
	if (id->id_length <= id->id_offset)
		return (-1);
	idlen = id->id_length - id->id_offset;
	idptr = id->id_data + id->id_offset;
	if ((idname = d2i_X509_NAME(NULL, &idptr, idlen)) == NULL)
		return (-1);
	if (X509_NAME_cmp(subject, idname) == 0)
		ret = 0;
	X509_NAME_free(idname);
	return (ret);
}

#define MODE_ALT_LOG	1
#define MODE_ALT_GET	2
#define MODE_ALT_CMP	3
int
ca_x509_subjectaltname_do(X509 *cert, int mode, const char *logmsg,
    struct iked_static_id *id, struct iked_id *retid)
{
	STACK_OF(GENERAL_NAME) *stack = NULL;
	GENERAL_NAME *entry;
	ASN1_STRING *cstr;
	char idstr[IKED_ID_SIZE];
	int idx, ret, i, type, len;
	uint8_t *data;

	ret = -1;
	idx = -1;
	while ((stack = X509_get_ext_d2i(cert, NID_subject_alt_name,
	    NULL, &idx)) != NULL) {
		for (i = 0; i < sk_GENERAL_NAME_num(stack); i++) {
			entry = sk_GENERAL_NAME_value(stack, i);
			switch (entry->type) {
			case GEN_DNS:
				cstr = entry->d.dNSName;
				if (ASN1_STRING_type(cstr) != V_ASN1_IA5STRING)
					continue;
				type = IKEV2_ID_FQDN;
				break;
			case GEN_EMAIL:
				cstr = entry->d.rfc822Name;
				if (ASN1_STRING_type(cstr) != V_ASN1_IA5STRING)
					continue;
				type = IKEV2_ID_UFQDN;
				break;
			case GEN_IPADD:
				cstr = entry->d.iPAddress;
				switch (ASN1_STRING_length(cstr)) {
				case 4:
					type = IKEV2_ID_IPV4;
					break;
				case 16:
					type = IKEV2_ID_IPV6;
					break;
				default:
					log_debug("%s: invalid subjectAltName"
					   " IP address", __func__);
					continue;
				}
				break;
			default:
				continue;
			}
			len = ASN1_STRING_length(cstr);
			data = ASN1_STRING_data(cstr);
			if (mode == MODE_ALT_LOG) {
				struct iked_id sanid;

				bzero(&sanid, sizeof(sanid));
				sanid.id_offset = 0;
				sanid.id_type = type;
				if ((sanid.id_buf = ibuf_new(data, len))
				    == NULL) {
					log_debug("%s: failed to get id buffer",
					     __func__);
					continue;
				}
				ikev2_print_id(&sanid, idstr, sizeof(idstr));
				log_info("%s: altname: %s", logmsg, idstr);
				ibuf_release(sanid.id_buf);
				sanid.id_buf = NULL;
			}
			/* Compare length and data */
			if (mode == MODE_ALT_CMP) {
				if (type == id->id_type &&
				    (len == (id->id_length - id->id_offset)) &&
				    (memcmp(id->id_data + id->id_offset,
				    data, len)) == 0) {
					ret = 0;
					break;
				}
			}
			/* Get first ID */
			if (mode == MODE_ALT_GET) {
				ibuf_release(retid->id_buf);
				if ((retid->id_buf = ibuf_new(data, len)) == NULL) {
					log_debug("%s: failed to get id buffer",
					    __func__);
					ret = -2;
					break;
				}
				retid->id_offset = 0;
				ikev2_print_id(retid, idstr, sizeof(idstr));
				log_debug("%s: %s", __func__, idstr);
				ret = 0;
				break;
			}
		}
		sk_GENERAL_NAME_pop_free(stack, GENERAL_NAME_free);
		if (ret != -1)
			break;
	}
	if (idx == -1)
		log_debug("%s: did not find subjectAltName in certificate",
		    __func__);
	return ret;
}

int
ca_x509_subjectaltname_log(X509 *cert, const char *logmsg)
{
	return ca_x509_subjectaltname_do(cert, MODE_ALT_LOG, logmsg, NULL, NULL);
}

int
ca_x509_subjectaltname_cmp(X509 *cert, struct iked_static_id *id)
{
	return ca_x509_subjectaltname_do(cert, MODE_ALT_CMP, NULL, id, NULL);
}

int
ca_x509_subjectaltname_get(X509 *cert, struct iked_id *retid)
{
	return ca_x509_subjectaltname_do(cert, MODE_ALT_GET, NULL, NULL, retid);
}

void
ca_sslinit(void)
{
	OpenSSL_add_all_algorithms();
	ERR_load_crypto_strings();

	/* Init hardware crypto engines. */
	ENGINE_load_builtin_engines();
	ENGINE_register_all_complete();
}

void
ca_sslerror(const char *caller)
{
	unsigned long	 error;

	while ((error = ERR_get_error()) != 0)
		log_warnx("%s: %s: %.100s", __func__, caller,
		    ERR_error_string(error, NULL));
}
