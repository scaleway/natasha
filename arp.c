#include <rte_arp.h>
#include <rte_ether.h>

#include "natasha.h"


static struct ether_hdr *
eth_header(struct rte_mbuf *pkt)
{
    return rte_pktmbuf_mtod((pkt), struct ether_hdr *);
}

static struct arp_hdr *
arp_header(struct rte_mbuf *pkt)
{
    unsigned char *p;

    p = rte_pktmbuf_mtod(pkt, unsigned char *) + sizeof(struct ether_hdr);
    return (struct arp_hdr *)p;
}

static int
arp_request(struct rte_mbuf *pkt, uint8_t port, struct core *core)
{
    uint32_t source_ip;
    uint32_t target_ip;
    struct ether_addr my_eth_addr;

    struct ether_hdr *eth_hdr = eth_header(pkt);
    struct arp_hdr *arp_hdr = arp_header(pkt);

    source_ip = rte_be_to_cpu_32(arp_hdr->arp_data.arp_sip);
    target_ip = rte_be_to_cpu_32(arp_hdr->arp_data.arp_tip);

    RTE_LOG(DEBUG, APP, "Port %d: Who has " IPv4_FMT "? asks " IPv4_FMT "\n",
            port, IPv4_FMTARGS(target_ip), IPv4_FMTARGS(source_ip)
    );

    // This request is not for me
    if (target_ip != core->app_config->ports[port].ip) {
        RTE_LOG(DEBUG, APP,
                "Port %d: not my IP address, ARP request ignored\n", port);
        return -1;
    }

    rte_eth_macaddr_get(port, &my_eth_addr);

    // Request for me, forge the reply
    arp_hdr->arp_op = rte_cpu_to_be_16(ARP_OP_REPLY);

    // Ethernet header: use query's source MAC address as destination
    ether_addr_copy(&eth_hdr->s_addr, &eth_hdr->d_addr);

    // Ethernet header: use our MAC address as source
    ether_addr_copy(&my_eth_addr, &eth_hdr->s_addr);


    // ARP header: use ARP query's source MAC address as destination
    ether_addr_copy(&arp_hdr->arp_data.arp_sha, &arp_hdr->arp_data.arp_tha);

    // ARP header: use our MAC address as source
    ether_addr_copy(&my_eth_addr, &arp_hdr->arp_data.arp_sha);

    // ARP header: use ARP query's source IP address as destination
    arp_hdr->arp_data.arp_tip = rte_cpu_to_be_32(source_ip);

    // ARP header: use our IP address as source
    arp_hdr->arp_data.arp_sip = rte_cpu_to_be_32(target_ip);

    RTE_LOG(
        DEBUG, APP, "Port %d: ARP Reply –"
                    " src MAC=" MAC_FMT " IP=" IPv4_FMT
                    " dst MAC=" MAC_FMT " IP=" IPv4_FMT "\n",
        port,

        MAC_FMTARGS(arp_hdr->arp_data.arp_sha),
        IPv4_FMTARGS(source_ip),

        MAC_FMTARGS(arp_hdr->arp_data.arp_tha),
        IPv4_FMTARGS(target_ip)
    );

    tx_send(pkt, port, &core->tx_queues[port]);
    tx_flush(port, &core->tx_queues[port]);

    // Do not free pkt
    return 0;
}

int
arp_handle(struct rte_mbuf *pkt, uint8_t port, struct core *core)
{

    switch (rte_be_to_cpu_16(arp_header(pkt)->arp_op)) {

    case ARP_OP_REQUEST:
        return arp_request(pkt, port, core);

    default:
        RTE_LOG(DEBUG, APP,
                "ARP packet received on port %d, but not of type "
                "ARP_OP_REQUEST – skip\n", port);
        break ;
    }
    return -1;
}
