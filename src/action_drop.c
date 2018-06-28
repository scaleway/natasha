#include "actions.h"


/*
 * Drop pkt inconditionnally, and stop processing next rules.
 */
int
action_drop(struct rte_mbuf *pkt, uint8_t port, struct core *core, void *data)
{
    core->stats->drop_nat_condition++;
    rte_pktmbuf_free(pkt);
    return -1;
}
