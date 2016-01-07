#include "action_drop.h"


/*
 * Drop pkt inconditionnally, and stop processing next rules.
 */
int
action_drop(struct rte_mbuf *pkt, uint8_t port, struct core *core, void *data)
{
    rte_pktmbuf_free(pkt);
    return -1;
}
