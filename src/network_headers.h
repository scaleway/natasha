/* vim: ts=4 sw=4 et */
#ifndef NETWORK_HEADERS_H_
#define NETWORK_HEADERS_H_

#include <rte_ether.h>
#include <rte_mbuf.h>

#include <rte_arp.h>
#include <rte_icmp.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_udp.h>

/* include linux headers for extra definitions */
#include <linux/icmp.h>

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

/* check if it's the first fragment by having MF flag and offset set to 0 */
#define NATA_IS_FIRST_FRAG(ipv4_hdr)                                \
    (((ipv4_hdr)->fragment_offset & htons(IPV4_HDR_MF_FLAG)) &&     \
     !((ipv4_hdr)->fragment_offset & htons(IPV4_HDR_OFFSET_MASK)))

/* Use offset field to check if it's a fragment */
#define NATA_IS_FRAG(ipv4_hdr) \
    (((ipv4_hdr)->fragment_offset & htons(IPV4_HDR_OFFSET_MASK)))

// last 12 bits of the TCI field
#define VLAN_ID(pkt) ((pkt)->vlan_tci & 0xfff)

/* incremental checksum update */
static inline void
cksum_update(uint16_t *csum, uint32_t from, uint32_t to)
{
    uint32_t sum, csum_c, from_c, res, res2, ret, ret2;

    csum_c = ~((uint32_t)*csum);
    from_c = ~from;
    res = csum_c + from_c;
    ret = res + (res < from_c);

    res2 = ret + to;
    ret2 = res2 + (res2 < to);

    sum = ret2;
    sum = (sum & 0xffff) + (sum >> 16);
    sum = (sum & 0xffff) + (sum >> 16);
    *csum = (uint16_t)~sum;

}
#endif
