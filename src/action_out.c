#include "natasha.h"
#include "network_headers.h"
#include "actions.h"


/*
 * Output an IPV4 packet on a TX queue.
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

    // PKT_TX_IPV4 needs to be set otherwise rte_eth_tx_prepare() called in
    // pkt.c/tx_flush will fail. See documentation in pkt.c.
    pkt->ol_flags |= PKT_TX_IPV4;

    // Rewrite out vlan
    pkt->vlan_tci = out->vlan;

    // Offload L4 checksums
    switch (ipv4_hdr->next_proto_id) {

    case IPPROTO_TCP: {
        struct tcp_hdr *tcp_hdr;

        tcp_hdr = tcp_header(pkt);
        tcp_hdr->cksum = 0;
        pkt->ol_flags |= PKT_TX_TCP_CKSUM;
        break ;
    }

    case IPPROTO_UDP: {
        struct udp_hdr *udp_hdr;

        // Frags csum are calculated during action_nat_rewrite_impl()
        if (unlikely(NATA_IS_FRAG(ipv4_hdr)))
            break;
        udp_hdr = udp_header(pkt);
        udp_hdr->dgram_cksum = 0;
        pkt->ol_flags |= PKT_TX_UDP_CKSUM;
        break ;
    }

    default:
        break ;
    }

    tx_send(pkt, out->port, &core->tx_queues[out->port], core->stats);
    return -1; // Stop processing rules
}
