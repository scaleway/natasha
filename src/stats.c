#include <stdio.h>

#include <rte_errno.h>
#include <rte_ethdev.h>

#include "natasha.h"


static int
eth_stats(uint8_t port, int fd)
{
    struct rte_eth_stats stats;
    int core;
    int queue_id;
    int ncores;

    if (rte_eth_stats_get(port, &stats) != 0) {
        RTE_LOG(ERR, APP, "Port %i: unable to get stats: %s\n",
                port, rte_strerror(rte_errno));
    }
    dprintf(fd,
        "Port %d: ipackets=%lu,opackets=%lu,ibytes=%lu,obytes=%lu,ierrors=%lu,"
        "oerrors=%lu,imissed=%lu,rx_nombuf=%lu\n",
        port,
        stats.ipackets, stats.opackets,
        stats.ibytes, stats.obytes,
        stats.ierrors, stats.oerrors,
        stats.imissed,
        stats.rx_nombuf
    );

    ncores = rte_lcore_count();
    queue_id = 0;
    RTE_LCORE_FOREACH_SLAVE(core) {
        // See to port initialization documentation to understand rx_stats_idx
        // and tx_stats_idx.
        const int rx_stats_idx = queue_id;
        const int tx_stats_idx = queue_id + ncores - 1;

        dprintf(fd,
                "Core %i RX queue=%d: q_ibytes=%lu,q_ipackets=%lu,q_obytes=%lu,"
                "q_opackets=%lu,q_errors=%lu\n",
                core, queue_id, stats.q_ibytes[rx_stats_idx],
                stats.q_ipackets[rx_stats_idx], stats.q_obytes[rx_stats_idx],
                stats.q_opackets[rx_stats_idx], stats.q_errors[rx_stats_idx]);

        dprintf(fd,
                "Core %i TX queue=%d: q_ibytes=%lu,q_ipackets=%lu,q_obytes=%lu,"
                "q_opackets=%lu,q_errors=%lu\n",
                core, queue_id, stats.q_ibytes[tx_stats_idx],
                stats.q_ipackets[tx_stats_idx], stats.q_obytes[tx_stats_idx],
                stats.q_opackets[tx_stats_idx], stats.q_errors[tx_stats_idx]);

        ++queue_id;
    }
    return 0;
}

/*
 * Display ports and queues statistics on stdout.
 */
void
stats_display(int fd)
{
    uint8_t port;

    for (port = 0; port < rte_eth_dev_count(); ++port) {
        eth_stats(port, fd);
    }
}

int
stats_reset(int fd)
{
    uint8_t port;

    for (port = 0; port < rte_eth_dev_count(); ++port) {
        rte_eth_stats_reset(port);
    }
    dprintf(fd, "Stats reset\n");
    return 0;
}
