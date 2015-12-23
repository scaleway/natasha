#ifndef ACTION_LOG_H_
#define ACTION_LOG_H_

#include <rte_mbuf.h>

#include "natasha.h"

int action_print(struct rte_mbuf *pkt, uint8_t port, struct core *core,
                 void *data);

#endif
