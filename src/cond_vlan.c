#include <rte_mbuf.h>

#include "natasha.h"
#include "network_headers.h"
#include "cond_vlan.h"


int
cond_vlan(struct rte_mbuf *pkt, uint8_t port, struct core *core, void *data)
{
    int *vlan = data;

    return VLAN_ID(pkt) == *vlan;
}
