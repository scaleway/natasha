#ifndef ACTION_DROP_H_
#define ACTION_DROP_H_

#include <rte_mbuf.h>

#include "natasha.h"

int action_drop(struct rte_mbuf *pkt, uint8_t port, struct core *core,
                void *data);

#endif
