#include "natasha.h"
#include "network_headers.h"
#include "action_out.h"


/*
 * Output a IPV4 packet on a TX queue.
 */
int
action_out(struct rte_mbuf *pkt, uint8_t port, struct core *core, void *data)
{
    struct out_packet *out = data;
    struct ether_hdr *eth_hdr = eth_header(pkt);
    struct ipv4_hdr *ipv4_hdr = ipv4_header(pkt);

    // setup ethernet header
    rte_eth_macaddr_get(port, &eth_hdr->s_addr);
    ether_addr_copy(&out->next_hop, &eth_hdr->d_addr);

    // Offload IPv4 checksum
    ipv4_hdr->hdr_checksum = 0;
    pkt->ol_flags |= PKT_TX_IP_CKSUM;

    // Rewrite out vlan
    pkt->vlan_tci = out->vlan;

    // Recompute L4 checksums
    switch (ipv4_hdr->next_proto_id) {

    case IPPROTO_TCP: {
        struct tcp_hdr *tcp_hdr;

        tcp_hdr = tcp_header(pkt);
        tcp_hdr->cksum = 0;
        pkt->ol_flags |= PKT_TX_TCP_CKSUM;
        tcp_hdr->cksum = rte_ipv4_phdr_cksum(ipv4_header(pkt),
                                             pkt->ol_flags);
        break ;
    }

    case IPPROTO_UDP: {
        struct udp_hdr *udp_hdr;

        udp_hdr = udp_header(pkt);
        udp_hdr->dgram_cksum = 0;
        pkt->ol_flags |= PKT_TX_UDP_CKSUM;
        udp_hdr->dgram_cksum = rte_ipv4_phdr_cksum(ipv4_header(pkt),
                                                   pkt->ol_flags);
        break ;
    }

    default:
        break ;
    }

    tx_send(pkt, out->port, &core->tx_queues[out->port]);
    return -1; // Stop processing rules
}
