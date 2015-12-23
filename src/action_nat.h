#ifndef COND_NAT_H_
#define COND_NAT_H_

#include "natasha.h"

// Field to rewrite in action_nat_rewrite.
static const int IPV4_SRC_ADDR = 0;
static const int IPV4_DST_ADDR = 1;

int action_nat_rewrite(struct rte_mbuf *pkt, uint8_t port, struct core *core,
                       void *data);

void nat_reset_lookup_table(uint32_t ***nat_lookup);
int add_rules_to_table(uint32_t ****nat_lookup, uint32_t int_ip, uint32_t ext_ip);
int nat_dump_rules(uint32_t ***nat_lookup);

#endif
