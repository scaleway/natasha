#ifndef NETWORK_HEADERS_H_
#define NETWORK_HEADERS_H_

#include <rte_ether.h>
#include <rte_mbuf.h>

#include <rte_arp.h>
#include <rte_icmp.h>
#include <rte_ip.h>


static inline struct ether_hdr *
eth_header(struct rte_mbuf *pkt)
{
    return rte_pktmbuf_mtod((pkt), struct ether_hdr *);
}

#define L2_HEADER(proto, type)                                                  \
    static inline type *                                                        \
    proto ## _header(struct rte_mbuf *pkt)                                      \
    {                                                                           \
        unsigned char *p;                                                       \
                                                                                \
        p = rte_pktmbuf_mtod(pkt, unsigned char *) + sizeof(struct ether_hdr);  \
        return (type *)p;                                                       \
    }

#define L3_HEADER(proto, type)                              \
    static inline type *                                    \
    proto ## _header(struct rte_mbuf *pkt)                  \
    {                                                       \
        struct ipv4_hdr *p;                                 \
                                                            \
        p = ipv4_header(pkt);                               \
        return (type *)((unsigned char *)p + sizeof(*p));   \
    }

L2_HEADER(arp, struct arp_hdr);
L2_HEADER(ipv4, struct ipv4_hdr);
L3_HEADER(icmp, struct icmp_hdr);

#undef L2_HEADER
#undef L3_HEADER

#endif
