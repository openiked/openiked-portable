/*	$OpenBSD: common.c,v 1.9 2020/11/26 22:29:32 tobhe Exp $ */
/*
 * A bunch of stub functions so we can compile and link ikev2_pld.c
 * in a standalone program for testing purposes.
 *
 * Placed in the public domain
 */

#include <sys/socket.h>
#include <sys/time.h>
#include <sys/uio.h>

#include <event.h>
#include <limits.h>
#include <string.h>

#include "iked.h"
#include "types.h"

#define IKEV2_FLAG_INITIATOR            0x08    /* Sent by the initiator */

int	 eap_parse(struct iked *, const struct iked_sa *,
	    struct iked_message *, void *, int);
int	 ikev2_msg_frompeer(struct iked_message *);
int	 ikev2_send_ike_e(struct iked *, struct iked_sa *, struct ibuf *,
	    u_int8_t, u_int8_t, int);
void	 ikev2_ikesa_recv_delete(struct iked *, struct iked_sa *);
struct iked_childsa *
	 childsa_lookup(struct iked_sa *, u_int64_t, u_int8_t);
int	  ikev2_childsa_delete(struct iked *, struct iked_sa *,
	    u_int8_t, u_int64_t, u_int64_t *, int);
int	 sa_stateok(const struct iked_sa *, int);
void	 sa_state(struct iked *, struct iked_sa *, int);
void	 ikev2_disable_rekeying(struct iked *, struct iked_sa *);
void	 ikev2_init_ike_sa(struct iked *, void *);
struct dh_group *
	 group_get(u_int32_t);
void	 timer_set(struct iked *, struct iked_timer *,
	     void (*)(struct iked *, void *), void *);
void	 timer_add(struct iked *, struct iked_timer *, int);
void	 timer_del(struct iked *, struct iked_timer *);
ssize_t	 ikev2_nat_detection(struct iked *, struct iked_message *,
	     void *, size_t, u_int, int);
int	 ca_setreq(struct iked *, struct iked_sa *, struct iked_static_id *,
	     u_int8_t, u_int8_t, u_int8_t *, size_t, enum privsep_procid);
int	 ikev2_print_id(struct iked_id *, char *, size_t);
int	 config_add_transform(struct iked_proposal *, u_int, u_int, u_int,
	     u_int);
struct iked_proposal *
	 config_add_proposal(struct iked_proposals *, u_int, u_int);
void	 config_free_proposal(struct iked_proposals *, struct iked_proposal *);
int	 ikev2_send_informational(struct iked *, struct iked_message *);
struct ibuf *
	 ikev2_msg_decrypt(struct iked *, struct iked_sa *, struct ibuf *,
	     struct ibuf *);
void ikev2_msg_cleanup(struct iked *, struct iked_message *);

int
eap_parse(struct iked *env, const struct iked_sa *sa, struct iked_message *msg,
    void *data, int response)
{
	return (0);
}

/* Copied from ikev2_msg.c for better coverage */
int
ikev2_msg_frompeer(struct iked_message *msg)
{
	struct iked_sa		*sa = msg->msg_sa;
	struct ike_header	*hdr;

	msg = msg->msg_parent;

	if (sa == NULL ||
	    (hdr = ibuf_seek(msg->msg_data, 0, sizeof(*hdr))) == NULL)
		return (0);

	if (!sa->sa_hdr.sh_initiator &&
	    (hdr->ike_flags & IKEV2_FLAG_INITIATOR))
		return (1);
	else if (sa->sa_hdr.sh_initiator &&
	    (hdr->ike_flags & IKEV2_FLAG_INITIATOR) == 0)
		return (1);

	return (0);
}

int
ikev2_send_ike_e(struct iked *env, struct iked_sa *sa, struct ibuf *buf,
    u_int8_t firstpayload, u_int8_t exchange, int response)
{
	return (0);
}

void
ikev2_ikesa_recv_delete(struct iked *env, struct iked_sa *sa)
{
}

const char *
ikev2_ikesa_info(uint64_t spi, const char *msg)
{
	return "";
}

struct iked_childsa *
childsa_lookup(struct iked_sa *a, u_int64_t b, u_int8_t c)
{
	return (NULL);
}

int
ikev2_childsa_delete(struct iked *a, struct iked_sa *b, u_int8_t c,
    u_int64_t d, u_int64_t *e , int f)
{
	return (0);
}

int
sa_stateok(const struct iked_sa *a, int b)
{
	return (0);
}

void
sa_state(struct iked * a, struct iked_sa *b, int c)
{
}

void
ikev2_disable_rekeying(struct iked *a, struct iked_sa *b)
{
}

void
ikev2_init_ike_sa(struct iked *a, void *b)
{
}

const struct group_id *
group_getid(u_int32_t id)
{
	return (NULL);
}

void
timer_set(struct iked *env, struct iked_timer *tmr,
    void (*cb)(struct iked *, void *), void *arg)
{
}

void
timer_add(struct iked *env, struct iked_timer *tmr, int timeout)
{
}

void
timer_del(struct iked *env, struct iked_timer *tmr)
{
}

ssize_t
ikev2_nat_detection(struct iked *env, struct iked_message *msg,
    void *ptr, size_t len, u_int type, int frompeer)
{
	bzero(ptr, len);
	return (0);
}

int
ca_setreq(struct iked *env, struct iked_sa *sh, struct iked_static_id *localid,
    u_int8_t type, u_int8_t more, u_int8_t *data, size_t len,
    enum privsep_procid procid)
{
	return (0);
}

int
ikev2_print_id(struct iked_id *id, char *idstr, size_t idstrlen)
{
	return (0);
}

int
config_add_transform(struct iked_proposal *prop, u_int type,
    u_int id, u_int length, u_int keylength)
{
	return (0);
}

struct iked_proposal *
config_add_proposal(struct iked_proposals *head, u_int id, u_int proto)
{
	return (NULL);
}

void
config_free_proposal(struct iked_proposals *head, struct iked_proposal *prop)
{
	return;
}

void config_free_fragments(struct iked_frag *frag)
{
	return;
}

int
ikev2_send_informational(struct iked *env, struct iked_message *msg)
{
	return (0);
}

struct ibuf *
ikev2_msg_decrypt(struct iked *env, struct iked_sa *sa,
    struct ibuf *msg, struct ibuf *src)
{
	if (src == NULL){
                fprintf(stderr, "%s\n", "msg_decrypt: src == NULL!");
                exit(-1);
        }

	/*
	 * Free src as caller uses ikev2_msg_decrypt() like this:
	 * src = ikev2_msg_decrypt(..., src);
	 */
	ibuf_free(src);	
	return (NULL);
}

void
ikev2_ike_sa_setreason(struct iked_sa *sa, char *r)
{
}

void
ikev2_msg_dispose(struct iked *env, struct iked_msgqueue *queue,
    struct iked_msg_retransmit *mr)
{
}

struct iked_msg_retransmit *
ikev2_msg_lookup(struct iked *env, struct iked_msgqueue *queue,
    struct iked_message *msg, uint8_t exchange)
{
	return NULL;
}

/* copied from ikev2_msg.c */
void
ikev2_msg_cleanup(struct iked *env, struct iked_message *msg)
{
	struct iked_certreq *cr;
	struct iked_proposal *prop, *proptmp;
	int			 i;

	if (msg == msg->msg_parent) {
		ibuf_free(msg->msg_nonce);
		ibuf_free(msg->msg_ke);
		ibuf_free(msg->msg_auth.id_buf);
		ibuf_free(msg->msg_peerid.id_buf);
		ibuf_free(msg->msg_localid.id_buf);
		ibuf_free(msg->msg_cert.id_buf);
		for (i = 0; i < IKED_SCERT_MAX; i++)
			ibuf_free(msg->msg_scert[i].id_buf);
		ibuf_free(msg->msg_cookie);
		ibuf_free(msg->msg_cookie2);
		ibuf_free(msg->msg_del_buf);
		free(msg->msg_eap.eam_user);
		free(msg->msg_cp_addr);
		free(msg->msg_cp_addr6);
		free(msg->msg_cp_dns);

		TAILQ_FOREACH_SAFE(prop, &msg->msg_proposals, prop_entry,
		    proptmp) {
			TAILQ_REMOVE(&msg->msg_proposals, prop, prop_entry);
			if (prop->prop_nxforms)
				free(prop->prop_xforms);
			free(prop);
		}

		msg->msg_nonce = NULL;
		msg->msg_ke = NULL;
		msg->msg_auth.id_buf = NULL;
		msg->msg_peerid.id_buf = NULL;
		msg->msg_localid.id_buf = NULL;
		msg->msg_cert.id_buf = NULL;
		for (i = 0; i < IKED_SCERT_MAX; i++)
			msg->msg_scert[i].id_buf = NULL;
		msg->msg_cookie = NULL;
		msg->msg_cookie2 = NULL;
		msg->msg_del_buf = NULL;
		msg->msg_eap.eam_user = NULL;
		msg->msg_cp_addr = NULL;
		msg->msg_cp_addr6 = NULL;
		msg->msg_cp_dns = NULL;

		while ((cr = SIMPLEQ_FIRST(&msg->msg_certreqs))) {
			ibuf_free(cr->cr_data);
			SIMPLEQ_REMOVE_HEAD(&msg->msg_certreqs, cr_entry);
			free(cr);
		}
	}

	if (msg->msg_data != NULL) {
		ibuf_free(msg->msg_data);
		msg->msg_data = NULL;
	}
}
