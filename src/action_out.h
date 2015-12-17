#ifndef ACTION_OUT_H_
#define ACTION_OUT_H_

#include <rte_ether.h>

struct out_packet {
    uint8_t port;
    int vlan;
    struct ether_addr next_hop;
};

RULE_ACTION action_out(struct rte_mbuf *pkt, uint8_t port, struct core *core,
                       void *data);

#endif
