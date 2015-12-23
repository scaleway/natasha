#include "natasha.h"
#include "network_headers.h"


int
action_print(struct rte_mbuf *pkt, uint8_t port, struct core *core, void *data)
{
    const struct ipv4_hdr *ipv4_hdr = ipv4_header(pkt);
    const int src_addr = rte_be_to_cpu_32(ipv4_hdr->src_addr);
    const int dst_addr = rte_be_to_cpu_32(ipv4_hdr->dst_addr);

    RTE_LOG(DEBUG, APP,
            "Port %i: packet on core %i from " IPv4_FMT " to " IPv4_FMT "\n",
            port, core->id, IPv4_FMTARGS(src_addr), IPv4_FMTARGS(dst_addr));

    return 0;
}
