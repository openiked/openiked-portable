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
 *	@(#)if_ether.h	8.1 (Berkeley) 6/10/93
 */

#ifndef _MSC_VER
#include_next <netinet/if_ether.h>
#else

#ifndef IKED_COMPAT_NETINET_IF_ETHER_H
#define IKED_COMPAT_NETINET_IF_ETHER_H


/*
 * Some basic Ethernet constants.
 */
#define	ETHER_ADDR_LEN	6	/* Ethernet address length		*/
#define ETHER_TYPE_LEN	2	/* Ethernet type field length		*/
#define ETHER_CRC_LEN	4	/* Ethernet CRC length			*/
#define ETHER_HDR_LEN	((ETHER_ADDR_LEN * 2) + ETHER_TYPE_LEN)
#define ETHER_MIN_LEN	64	/* Minimum frame length, CRC included	*/
#define ETHER_MAX_LEN	1518	/* Maximum frame length, CRC included	*/
#define ETHER_MAX_DIX_LEN	1536	/* Maximum DIX frame length	*/

/*
 * Some Ethernet extensions.
 */
#define ETHER_VLAN_ENCAP_LEN	4	/* len of 802.1Q VLAN encapsulation */

/*
 * Mbuf adjust factor to force 32-bit alignment of IP header.
 * Drivers should do m_adj(m, ETHER_ALIGN) when setting up a
 * receive so the upper layers get the IP header properly aligned
 * past the 14-byte Ethernet header.
 */
#define ETHER_ALIGN	2	/* driver adjust for IP hdr alignment */

/*
 * The maximum supported Ethernet length and some space for encapsulation.
 */
#define ETHER_MAX_HARDMTU_LEN	65435

/*
 * Ethernet address - 6 octets
 */
struct ether_addr {
	uint8_t ether_addr_octet[ETHER_ADDR_LEN];
};

/*
 * The length of the combined header.
 */
struct	ether_header {
	uint8_t  ether_dhost[ETHER_ADDR_LEN];
	uint8_t  ether_shost[ETHER_ADDR_LEN];
	uint16_t ether_type;
};

/*
 * VLAN headers.
 */

struct  ether_vlan_header {
        uchar  evl_dhost[ETHER_ADDR_LEN];
        uchar  evl_shost[ETHER_ADDR_LEN];
        uint16_t evl_encap_proto;
        uint16_t evl_tag;
        uint16_t evl_proto;
};

/*
 * Ethernet Address Resolution Protocol.
 *
 * See RFC 826 for protocol description.  Structure below is adapted
 * to resolving internet addresses.  Field names used correspond to
 * RFC 826.
 */
struct	ether_arp {
	struct	 arphdr ea_hdr;			/* fixed-size header */
	uint8_t arp_sha[ETHER_ADDR_LEN];	/* sender hardware address */
	uint8_t arp_spa[4];			/* sender protocol address */
	uint8_t arp_tha[ETHER_ADDR_LEN];	/* target hardware address */
	uint8_t arp_tpa[4];			/* target protocol address */
};
#define	arp_hrd	ea_hdr.ar_hrd
#define	arp_pro	ea_hdr.ar_pro
#define	arp_hln	ea_hdr.ar_hln
#define	arp_pln	ea_hdr.ar_pln
#define	arp_op	ea_hdr.ar_op

#endif /* !IKED_COMPAT_NETINET_IF_ETHER_H */

#endif
