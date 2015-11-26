#ifndef PKT_H_
#define PKT_H_

#include <rte_mbuf.h>

#include "core.h"

int handle_packet(struct rte_mbuf *pkt, struct core *core);

#endif
