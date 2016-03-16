#ifndef CONDS_H_
#define CONDS_H_

#include "natasha.h"

/****************
 * cond network *
 ****************/

// struct ipv4_network is the void *data of cond_ipv4_*() functions
struct ipv4_network {
    uint32_t ip;
    uint32_t mask;
};

// Rule conditions
int cond_ipv4_src_in_network(struct rte_mbuf *pkt, uint8_t port,
                             struct core *core, void *data);

int cond_ipv4_dst_in_network(struct rte_mbuf *pkt, uint8_t port,
                             struct core *core, void *data);


/*************
 * cond vlan *
 *************/

// the void *data of cond_vlan() is the (int *) TCI field
int cond_vlan(struct rte_mbuf *pkt, uint8_t port, struct core *core,
              void *data);

#endif
