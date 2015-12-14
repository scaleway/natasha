#include <rte_ip.h>

#include "natasha.h"
#include "network_headers.h"


static RULE_ACTION
display_pkt(struct rte_mbuf *pkt, uint8_t port, struct core *core, void *data)
{
    const struct ipv4_hdr *ipv4_hdr = ipv4_header(pkt);

    RTE_LOG(DEBUG, APP,
            "Port %i: packet from " IPv4_FMT " to " IPv4_FMT "\n",
            port,
            IPv4_FMTARGS(ipv4_hdr->src_addr),
            IPv4_FMTARGS(ipv4_hdr->dst_addr));

    return ACTION_NEXT;
}

int
app_config_parse(int argc, char **argv, struct app_config *config)
{
    memset(config, 0, sizeof(*config));

    config->ports[0].ip = IPv4(10, 2, 31, 11);
    config->ports[1].ip = IPv4(212, 47, 255, 91);

    config->rules[0].actions[0].f = display_pkt;
    return 0;
}
