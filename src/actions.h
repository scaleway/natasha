/* vim: ts=4 sw=4 et */
#ifndef ACTIONS_H_
#define ACTIONS_H_

#include "natasha.h"


/***************
 * action drop *
 ***************/

int action_drop(struct rte_mbuf *pkt, uint8_t port, struct core *core,
                void *data);


/**************
 * action log *
 **************/

int action_print(struct rte_mbuf *pkt, uint8_t port, struct core *core,
                 void *data);


/**************
 * action nat *
 **************/

// Field to rewrite in action_nat_rewrite.
static const int IPV4_SRC_ADDR = 0;
static const int IPV4_DST_ADDR = 1;

int action_nat_rewrite(struct rte_mbuf *pkt, uint8_t port, struct core *core,
                       void *data);

void nat_reset_lookup_table(uint32_t ***nat_lookup);

int add_rules_to_table(uint32_t ****nat_lookup,
                       uint32_t int_ip, uint32_t ext_ip,
                       unsigned int socket_id);

int nat_dump_rules(int out_fd, uint32_t ***nat_lookup);
int nat_number_of_rules(uint32_t ***nat_lookup);

struct out_packet {
    uint8_t port;
    int vlan;
    struct ether_addr next_hop;
};


/**************
 * action out *
 **************/

int action_out(struct rte_mbuf *pkt, uint8_t port, struct core *core,
               void *data);

#endif
