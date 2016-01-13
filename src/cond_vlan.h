#ifndef COND_VLAN_H_
#define COND_VLAN_H_

int cond_vlan(struct rte_mbuf *pkt, uint8_t port, struct core *core,
              void *data);


#endif
