/*
 * Copyright (c) 1982, 1986, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)ip.h	8.1 (Berkeley) 6/10/93
 */

#ifndef _MSC_VER
#include_next <netinet/ip.h>
#else

#ifndef IKED_COMPAT_NETINET_IP_H
#define IKED_COMPAT_NETINET_IP_H

/*
 * Definitions for internet protocol version 4.
 * Per RFC 791, September 1981.
 */
#define	IPVERSION	4

/*
 * Structure of an internet header, naked of options.
 */
#pragma pack(push,1)
struct ip {
#if _BYTE_ORDER == _LITTLE_ENDIAN
	uint8_t   ip_hl:4,		/* header length */
		  ip_v:4;		/* version */
#elif _BYTE_ORDER == _BIG_ENDIAN
	uint8_t   ip_v:4,		/* version */
		  ip_hl:4;		/* header length */
#endif
	uint8_t   ip_tos;		/* type of service */
	uint16_t  ip_len;		/* total length */
	uint16_t  ip_id;		/* identification */
	uint16_t  ip_off;		/* fragment offset field */
#define	IP_RF 0x8000			/* reserved fragment flag */
#define	IP_DF 0x4000			/* dont fragment flag */
#define	IP_MF 0x2000			/* more fragments flag */
#define	IP_OFFMASK 0x1fff		/* mask for fragmenting bits */
	uint8_t   ip_ttl;		/* time to live */
	uint8_t   ip_p;			/* protocol */
	uint16_t  ip_sum;		/* checksum */
	struct	  in_addr ip_src, ip_dst; /* source and dest address */
};
#pragma pack(pop)

#define	IP_MAXPACKET	65535		/* maximum packet size */

/*
 * Definitions for IP type of service (ip_tos)
 */
#define	IPTOS_LOWDELAY		0x10
#define	IPTOS_THROUGHPUT	0x08
#define	IPTOS_RELIABILITY	0x04
/*	IPTOS_LOWCOST		0x02 XXX */
#if 1
/* ECN RFC3168 obsoletes RFC2481, and these will be deprecated soon. */
#define	IPTOS_CE		0x01	/* congestion experienced */
#define	IPTOS_ECT		0x02	/* ECN-capable transport */
#endif

#endif /* !IKED_COMPAT_NETINET_IP_H */

#endif
