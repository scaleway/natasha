#ifndef COND_NETWORK_H_
#define COND_NETWORK_H_

struct ipv4_network {
    uint32_t ip;
    uint32_t mask;
};

// Rule conditions
int cond_ipv4_src_in_network(struct rte_mbuf *pkt, uint8_t port,
                             struct core *core, void *data);

int cond_ipv4_dst_in_network(struct rte_mbuf *pkt, uint8_t port,
                             struct core *core, void *data);

#endif
