#include <stdio.h>

#include <rte_errno.h>
#include <rte_ethdev.h>
#include <rte_cycles.h>

#include "natasha.h"


static int
eth_stats(uint8_t port, int fd)
{
	uint64_t diff_pkts_rx, diff_pkts_tx, diff_cycles;
	static uint64_t prev_bytes_rx, prev_bytes_tx;
	static uint64_t prev_pkts_rx, prev_pkts_tx;
	uint64_t diff_bytes_rx, diff_bytes_tx;
	static uint64_t prev_cycles;
	uint64_t pps_rx, pps_tx;
	uint64_t bps_rx, bps_tx;
    struct rte_eth_stats stats;

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

    if (support_per_queue_statistics(port)) {
        int core;
        int queue_id;
        int ncores;

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
    }

    diff_cycles = prev_cycles;
    prev_cycles = rte_rdtsc();
    if (diff_cycles > 0)
        diff_cycles = prev_cycles - diff_cycles;

    diff_pkts_rx = (stats.ipackets > prev_pkts_rx) ?
        (stats.ipackets - prev_pkts_rx) : 0;
    diff_pkts_tx = (stats.opackets > prev_pkts_tx) ?
        (stats.opackets - prev_pkts_tx) : 0;

    prev_pkts_rx = stats.ipackets;
    prev_pkts_tx = stats.opackets;

    pps_rx = diff_cycles > 0 ?
        diff_pkts_rx * rte_get_tsc_hz() / diff_cycles : 0;
    pps_tx = diff_cycles > 0 ?
        diff_pkts_tx * rte_get_tsc_hz() / diff_cycles : 0;

    diff_bytes_rx = (stats.ibytes > prev_bytes_rx) ?
        (stats.ibytes - prev_bytes_rx) : 0;
    diff_bytes_tx = (stats.obytes > prev_bytes_tx) ?
        (stats.obytes - prev_bytes_tx) : 0;

    prev_bytes_rx = stats.ibytes;
    prev_bytes_tx = stats.obytes;

    bps_rx = diff_cycles > 0 ?
        diff_bytes_rx * rte_get_tsc_hz() / diff_cycles : 0;
    bps_tx = diff_cycles > 0 ?
        diff_bytes_tx * rte_get_tsc_hz() / diff_cycles : 0;

#define BYTES_TO_BITS   8
    dprintf(fd, "Throughput (since last show)\n\n");
    dprintf(fd, "RX:\t%12"PRIu64" PPS \t%12"PRIu64" bits/s\n",
            pps_rx, bps_rx * BYTES_TO_BITS);
    dprintf(fd, "TX:\t%12"PRIu64" PPS \t%12"PRIu64" bits/s\n",
            pps_tx, bps_tx * BYTES_TO_BITS);

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

/*
 * Display nat rules on stdout.
 */
int
rules_display(int fd, struct core *cores)
{

    struct nat_address ***nat_lookup;
    uint32_t from, to;
    int i, j, k, core;

    /*
     * Use this loop just to get the first lcore slave and use it to get
     * access to the shared app_config.
     */
    RTE_LCORE_FOREACH_SLAVE(core) {
        nat_lookup = cores[core].app_config->nat_lookup;

        if (nat_lookup == NULL) {
            dprintf(fd, "Cannot access to shared memory\n");
            return -1;
        }

        for (i = 0; i < lkp_fs; ++i) {
            if (nat_lookup[i] == NULL)
                continue;

            for (j = 0; j < lkp_ss; ++j) {
                if (nat_lookup[i][j] == NULL)
                    continue;

                for (k = 0; k < lkp_ts; ++k) {
                    if (nat_lookup[i][j][k].address == 0)
                        continue;

                    from = ((i & 0xff) << 24) |
                           ((j & 0xff) << 16) |
                           (k & 0xffff);
                    to = rte_be_to_cpu_32(nat_lookup[i][j][k].address);
                    dprintf(fd,
                            IPv4_FMT " -> " IPv4_FMT " \t%" PRIu64 " bytes\n",
                            IPv4_FMTARGS(from), IPv4_FMTARGS(to),
                            nat_lookup[i][j][k].bytes);
                }
            }
        }
        break;
    }
    return 0;
}
