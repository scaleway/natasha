/* vim: ts=4 sw=4 et */
#include <rte_mbuf.h>

#include "conds.h"
#include "natasha.h"
#include "network_headers.h"


/*
 * @return
 *  - true if ip is in network
 */
static inline int
ipv4_in_network(uint32_t ip, struct ipv4_network *network)
{
    return (ip & (~0 << (32 - network->mask))) ==
        (network->ip & (~0 << (32 - network->mask)));
}

int
cond_ipv4_dst_in_network(struct rte_mbuf *pkt, uint8_t port, struct core *core,
                         void *data)
{
    return ipv4_in_network(rte_be_to_cpu_32(ipv4_header(pkt)->dst_addr), data);
}

int
cond_ipv4_src_in_network(struct rte_mbuf *pkt, uint8_t port, struct core *core,
                         void *data)
{
    return ipv4_in_network(rte_be_to_cpu_32(ipv4_header(pkt)->src_addr), data);
}
