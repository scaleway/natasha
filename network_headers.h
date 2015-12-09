#ifndef NETWORK_HEADERS_H_
#define NETWORK_HEADERS_H_

#include <rte_ether.h>
#include <rte_mbuf.h>

#include <rte_arp.h>
#include <rte_ip.h>


static inline struct ether_hdr *
eth_header(struct rte_mbuf *pkt)
{
    return rte_pktmbuf_mtod((pkt), struct ether_hdr *);
}

#define _CREATE_FUNC_FOR_PROTO(proto, type) \
    static inline type *                                                        \
    proto ## _header(struct rte_mbuf *pkt)                                      \
    {                                                                           \
        unsigned char *p;                                                       \
                                                                                \
        p = rte_pktmbuf_mtod(pkt, unsigned char *) + sizeof(struct ether_hdr);  \
        return (type *)p;                                                       \
    }

_CREATE_FUNC_FOR_PROTO(arp, struct arp_hdr);
_CREATE_FUNC_FOR_PROTO(ipv4, struct ipv4_hdr);

#undef _CREATE_FUNC_FOR_PROTO

#endif
