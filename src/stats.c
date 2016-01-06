#include <stdio.h>

#include <rte_errno.h>
#include <rte_ethdev.h>

#include "natasha.h"


static int
eth_stats(uint8_t port)
{
    struct rte_eth_stats stats;

    if (rte_eth_stats_get(port, &stats) != 0) {
        RTE_LOG(ERR, APP, "Port %i: unable to get stats: %s\n",
                port, rte_strerror(rte_errno));
    }
    printf(
        "Port %d: ipackets=%lu,opackets=%lu,ibytes=%lu,obytes=%lu,ierrors=%lu,"
        "oerrors=%lu,imissed=%lu,ibadcrc=%lu,ibadlen=%lu,rx_nombuf=%lu\n",
        port,
        stats.ipackets, stats.opackets,
        stats.ibytes, stats.obytes,
        stats.ierrors, stats.oerrors,
        stats.imissed, stats.ibadcrc, stats.ibadlen,
        stats.rx_nombuf
    );
    return 0;
}


/*
 * Display ports and queues statistics on stdout.
 */
void
stats_display(int sig)
{
    uint8_t port;

    for (port = 0; port < rte_eth_dev_count(); ++port) {
        eth_stats(port);
    }
}
