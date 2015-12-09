#include <rte_ethdev.h>

#include "natasha.h"
#include "network_headers.h"


int
ipv4_handle(struct rte_mbuf *pkt, uint8_t port, struct core *core)
{
    struct ipv4_hdr *ipv4_hdr;

    ipv4_hdr = ipv4_header(pkt);

    if (ipv4_hdr->time_to_live > 0) {
        ipv4_hdr->time_to_live--;
    }

    switch (ipv4_hdr->next_proto_id) {

    case IPPROTO_ICMP:
        break ;

    default:
        break ;

    }

    return -1;
}
