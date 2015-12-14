#include "natasha.h"
#include "network_headers.h"


RULE_ACTION
action_print(struct rte_mbuf *pkt, uint8_t port, struct core *core, void *data)
{
    const struct ipv4_hdr *ipv4_hdr = ipv4_header(pkt);

    RTE_LOG(DEBUG, APP,
            "Port %i: packet on core %i from " IPv4_FMT " to " IPv4_FMT "\n",
            port,
            core->id,
            IPv4_FMTARGS(ipv4_hdr->src_addr),
            IPv4_FMTARGS(ipv4_hdr->dst_addr));

    return ACTION_NEXT;
}
