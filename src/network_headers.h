#ifndef NETWORK_HEADERS_H_
#define NETWORK_HEADERS_H_

#include <rte_ether.h>
#include <rte_mbuf.h>

#include <rte_arp.h>
#include <rte_icmp.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_udp.h>


static inline struct ether_hdr *
eth_header(struct rte_mbuf *pkt)
{
    return rte_pktmbuf_mtod((pkt), struct ether_hdr *);
}

#define L3_HEADER(proto, type)                                                  \
    static inline type *                                                        \
    proto ## _header(struct rte_mbuf *pkt)                                      \
    {                                                                           \
        unsigned char *p;                                                       \
                                                                                \
        p = rte_pktmbuf_mtod(pkt, unsigned char *) + sizeof(struct ether_hdr);  \
        return (type *)p;                                                       \
    }

#define L4_HEADER(proto, type)                              \
    static inline type *                                    \
    proto ## _header(struct rte_mbuf *pkt)                  \
    {                                                       \
        struct ipv4_hdr *p;                                 \
                                                            \
        p = ipv4_header(pkt);                               \
        return (type *)((unsigned char *)p + sizeof(*p));   \
    }

L3_HEADER(arp, struct arp_hdr);
L3_HEADER(ipv4, struct ipv4_hdr);

L4_HEADER(icmp, struct icmp_hdr);
L4_HEADER(tcp, struct tcp_hdr);
L4_HEADER(udp, struct udp_hdr);

#undef L2_HEADER
#undef L3_HEADER

#define NATA_FIRST_FRAG(ipv4_hdr) \
    (((ipv4_hdr)->fragment_offset & (IPV4_HDR_MF_FLAG + IPV4_HDR_OFFSET_MASK)) == htons(IPV4_HDR_MF_FLAG))

#define NATA_IS_FRAG(ipv4_hdr) \
    (((ipv4_hdr)->fragment_offset & (IPV4_HDR_MF_FLAG + IPV4_HDR_OFFSET_MASK)) != 0)

// last 12 bits of the TCI field
#define VLAN_ID(pkt) ((pkt)->vlan_tci & 0xfff)

#endif
