#include "natasha.h"
#include "network_headers.h"
#include "action_out.h"


/*
 * Output a packet on a TX queue.
 */
RULE_ACTION
action_out(struct rte_mbuf *pkt, uint8_t port, struct core *core, void *data)
{
    struct out_packet *out = data;
    struct ether_hdr *eth_hdr = eth_header(pkt);

    // setup ethernet header
    rte_eth_macaddr_get(port, &eth_hdr->s_addr);
    ether_addr_copy(&out->next_hop, &eth_hdr->d_addr);

    // Offloat IPv4 checksum
    ipv4_header(pkt)->hdr_checksum = 0;
    pkt->ol_flags |= PKT_TX_IP_CKSUM;

    // recompute udp header if udp
    // recompute tcp header if tcp

    tx_send(pkt, out->port, &core->tx_queues[out->port]);
    return ACTION_BREAK;
}
