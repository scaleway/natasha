#include <rte_arp.h>
#include <rte_ether.h>

#include "natasha.h"


static int
arp_request(struct ether_hdr *eth_hdr, struct arp_hdr *arp_hdr, uint8_t port,
            struct core *core)
{
    RTE_LOG(DEBUG, APP, "Who has " IPv4_FMT "? asks " IPv4_FMT "\n",
            IPv4_FMTARGS(rte_cpu_to_be_32(arp_hdr->arp_data.arp_tip)),
            IPv4_FMTARGS(rte_cpu_to_be_32(arp_hdr->arp_data.arp_sip))
    );

    return -1;
}

int
arp_handle(struct rte_mbuf *pkt, uint8_t port, struct core *core)
{
    struct ether_hdr *eth_hdr;
    struct arp_hdr *arp_hdr;

    eth_hdr = rte_pktmbuf_mtod(pkt, struct ether_hdr *);
    arp_hdr = (struct arp_hdr *)(rte_pktmbuf_mtod(pkt, unsigned char *) +
                                 sizeof(struct ether_hdr));

    switch (rte_be_to_cpu_16(arp_hdr->arp_op)) {

    case ARP_OP_REQUEST:
        return arp_request(eth_hdr, arp_hdr, port, core);

    default:
        RTE_LOG(DEBUG, APP,
                "ARP packet received on port %d, but not of type "
                "ARP_OP_REQUEST – skip\n", port);
        break ;
    }
    return -1;
}
