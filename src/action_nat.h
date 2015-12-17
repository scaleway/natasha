#ifndef COND_NAT_H_
#define COND_NAT_H_

#include "natasha.h"

// Field to rewrite in action_nat_rewrite.
typedef enum {
    IPV4_SRC_ADDR,
    IPV4_DST_ADDR,
} nat_rewrite_field_t;


RULE_ACTION action_nat_rewrite(struct rte_mbuf *pkt, uint8_t port, struct core *core,
                               void *data);

int nat_reload(uint32_t ****nat_lookup, const char *filename);
int nat_dump_rules(uint32_t ***nat_lookup);

#endif
