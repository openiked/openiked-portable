/*	$OpenBSD$ */
/*
 * Fuzz tests for payload parsing
 *
 * Placed in the public domain
 */

#include <sys/socket.h>
#include <sys/queue.h>
#include <sys/uio.h>

#include <event.h>
#include <imsg.h>
#include <string.h>

#include "iked.h"
#include "ikev2.h"

u_int8_t cookies[] = {
	0xde, 0xad, 0xbe, 0xef, 0xca, 0xfe, 0x00, 0x01,	/* initator cookie */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00	/* responder cookie */
};

u_int8_t genhdr[] = {
	0x00, 0x20, 0x22, 0x08,	/* next, major/minor, exchange type, flags */
	0x00, 0x00, 0x00, 0x00,	/* message ID */
	0x00, 0x00, 0x00, 0x00	/* total length */
};

#define OFFSET_ICOOKIE		0
#define OFFSET_RCOOKIE		8
#define OFFSET_NEXTPAYLOAD	(0 + sizeof(cookies))
#define OFFSET_VERSION		(1 + sizeof(cookies))
#define OFFSET_EXCHANGE		(2 + sizeof(cookies))
#define OFFSET_LENGTH		(8 + sizeof(cookies))

static u_int8_t *
get_icookie(u_int8_t *data)
{
	return &data[OFFSET_ICOOKIE];
}

static u_int8_t *
get_rcookie(u_int8_t *data)
{
	return &data[OFFSET_RCOOKIE];
}

static u_int8_t
get_nextpayload(u_int8_t *data)
{
	return data[OFFSET_NEXTPAYLOAD];
}

static u_int8_t
get_version(u_int8_t *data)
{
	return data[OFFSET_VERSION];
}

static u_int8_t
get_exchange(u_int8_t *data)
{
	return data[OFFSET_EXCHANGE];
}

static u_int32_t
get_length(u_int8_t *data)
{
	return *(u_int32_t *)&data[OFFSET_LENGTH];
}

static void
prepare_header(struct ike_header *hdr, struct ibuf *data)
{
	bzero(hdr, sizeof(*hdr));
	bcopy(get_icookie(ibuf_data(data)), &hdr->ike_ispi,
	    sizeof(hdr->ike_ispi));
	bcopy(get_rcookie(ibuf_data(data)), &hdr->ike_rspi,
	    sizeof(hdr->ike_rspi));
	hdr->ike_nextpayload = get_nextpayload(ibuf_data(data));
	hdr->ike_version = get_version(ibuf_data(data));
	hdr->ike_exchange = get_exchange(ibuf_data(data));
	hdr->ike_length = get_length(ibuf_data(data));
}

static void
prepare_message(struct iked_message *msg, struct ibuf *data)
{
	static struct iked_sa	sa;

	bzero(&sa, sizeof(sa));
	bzero(msg, sizeof(*msg));

	msg->msg_sa = &sa;
	msg->msg_data = data;
	msg->msg_e = 1;
	msg->msg_parent = msg;

	TAILQ_INIT(&msg->msg_proposals);
	SIMPLEQ_INIT(&msg->msg_certreqs);
}

/* Entry-Point for libFuzzer */
int
LLVMFuzzerTestOneInput(const char *data, size_t size)
{
	struct ibuf		*fuzzed;
	struct ike_header	 hdr;
	struct iked_message	 msg;

	bzero(&hdr, sizeof(hdr));
	bzero(&msg, sizeof(msg));

	fuzzed = ibuf_new(data, size);
	if (fuzzed == NULL){
		fprintf(stderr, "%s\n", "ERROR: fuzzed == NULL! "
		    "(hint: fuzz-input too long?)");
		return -1;
	}	
	
	/* size too small? */
	if (size < sizeof(cookies) + sizeof(genhdr)){
		ibuf_free(fuzzed);
		return 0;
	}	       

	prepare_header(&hdr, fuzzed);
	prepare_message(&msg, fuzzed);

	ikev2_pld_parse(NULL, &hdr, &msg, 0);

	ikev2_msg_cleanup(NULL, &msg);

	return 0;
}
