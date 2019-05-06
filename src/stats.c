/* vim: ts=4 sw=4 et */
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

#define BYTES_TO_BITS           8
#define MILLION                 (uint64_t)(1000000ULL)
#define INTER_FRAME_GAP         12
#define PKT_PREAMBLE            8
#define PKT_OVERHEAD            (INTER_FRAME_GAP + PKT_PREAMBLE)
    diff_bytes_rx = (stats.ibytes > prev_bytes_rx) ?
        (stats.ibytes - prev_bytes_rx) : 0;
    diff_bytes_rx_accurate = diff_bytes_rx + (pps_rx * PKT_OVERHEAD);

    diff_bytes_tx = (stats.obytes > prev_bytes_tx) ?
        (stats.obytes - prev_bytes_tx) : 0;
    diff_bytes_tx_accurate = diff_bytes_tx + (pps_tx * PKT_OVERHEAD);

    prev_bytes_rx = stats.ibytes;
    prev_bytes_tx = stats.obytes;

    bps_rx = diff_cycles > 0 ?
        diff_bytes_rx_accurate * rte_get_tsc_hz() / diff_cycles : 0;
    bps_tx = diff_cycles > 0 ?
        diff_bytes_tx_accurate * rte_get_tsc_hz() / diff_cycles : 0;

    dprintf(fd, "Throughput (since last show)\n\n");
    dprintf(fd, "RX:\t%12"PRIu64" PPS \t%12"PRIu64" mbits/s\n",
            pps_rx,
            bps_rx * BYTES_TO_BITS / MILLION);
    dprintf(fd, "TX:\t%12"PRIu64" PPS \t%12"PRIu64" mbits/s\n",
            pps_tx,
            bps_tx * BYTES_TO_BITS / MILLION);

    return 0;
}

/*
 * Display DPDK ports and queues statistics on stdout.
 */
void
stats_display(int fd)
{
    uint8_t port;

    for (port = 0; port < rte_eth_dev_count(); ++port) {
        eth_stats(port, fd);
    }
}

/*
 * Display NATASHA application statistics.
 */
void
xstats_display(int fd, struct core *cores)
{
    struct rte_eth_xstat_name *xstats_names;
    struct natasha_app_stats global_stats, *s;
    struct rte_eth_xstat *xstats;
    int cnt_xstats, idx_xstat;
    uint8_t coreid;
    uint8_t portid;

    memset(&global_stats, 0, sizeof(global_stats));
    dprintf(fd, " --- NATASHA stats ---\n");
    RTE_LCORE_FOREACH_SLAVE(coreid) {
        s = cores[coreid].stats;
        dprintf(fd, "Core%u:\t"
                "drop_no_rule=%" PRId64 ", drop_nat_condition=%u, "
                "drop_tx_notsent=%u, drop_bad_l3_cksum=%u, "
                "rx_bad_l4_cksum=%u, drop_unhandled_ethertype=%u, "
                "drop_unknown_icmp=%u\n", coreid,
                s->drop_no_rule, s->drop_nat_condition, s->drop_tx_notsent,
                s->drop_bad_l3_cksum, s->rx_bad_l4_cksum,
                s->drop_unhandled_ethertype, s->drop_unknown_icmp);
        global_stats.drop_no_rule               += s->drop_no_rule;
        global_stats.drop_nat_condition         += s->drop_nat_condition;
        global_stats.drop_tx_notsent            += s->drop_tx_notsent;
        global_stats.drop_bad_l3_cksum          += s->drop_bad_l3_cksum;
        global_stats.rx_bad_l4_cksum            += s->rx_bad_l4_cksum;
        global_stats.drop_unknown_icmp          += s->drop_unknown_icmp;
        global_stats.drop_unhandled_ethertype   += s->drop_unhandled_ethertype;
    }
    dprintf(fd, "Global:\t"
            "drop_no_rule=%" PRId64 ", drop_nat_condition=%u, "
            "drop_tx_notsent=%u, drop_bad_l3_cksum=%u, rx_bad_l4_cksum=%u,"
            " drop_unhandled_ethertype=%u, drop_unknown_icmp=%u\n",
            global_stats.drop_no_rule, global_stats.drop_nat_condition,
            global_stats.drop_tx_notsent, global_stats.drop_bad_l3_cksum,
            global_stats.rx_bad_l4_cksum, global_stats.drop_unknown_icmp,
            global_stats.drop_unhandled_ethertype);

    for (portid = 0; portid < rte_eth_dev_count(); ++portid) {
        /* print common stats */
        eth_stats(portid, fd);

        if (!rte_eth_dev_is_valid_port(portid)) {
            RTE_LOG(INFO, APP, "Error: Invalid port number %i\n", portid);
            return;
        }

        /* Get count */
        cnt_xstats = rte_eth_xstats_get_names(portid, NULL, 0);
        if (cnt_xstats  < 0) {
            RTE_LOG(INFO, APP, "Error: Cannot get count of xstats\n");
            return;
        }

        /* Get id-name lookup table */
        xstats_names = malloc(sizeof(struct rte_eth_xstat_name) * cnt_xstats);
        if (xstats_names == NULL) {
            RTE_LOG(INFO, APP, "Cannot allocate memory for xstats lookup\n");
            return;
        }
        if (cnt_xstats != rte_eth_xstats_get_names(portid, xstats_names, cnt_xstats)) {
            RTE_LOG(INFO, APP, "Error: Cannot get xstats lookup\n");
            free(xstats_names);
            return;
        }

        /* Get stats themselves */
        xstats = malloc(sizeof(struct rte_eth_xstat) * cnt_xstats);
        if (xstats == NULL) {
            RTE_LOG(INFO, APP, "Cannot allocate memory for xstats\n");
            free(xstats_names);
            return;
        }
        if (cnt_xstats != rte_eth_xstats_get(portid, xstats, cnt_xstats)) {
            RTE_LOG(INFO, APP, "Error: Unable to get xstats\n");
            free(xstats_names);
            free(xstats);
            return;
        }

        dprintf(fd, " --- DPDK extra stats ---\n");
        dprintf(fd, "Port %d: ", portid);
        /* Display non zero xstats */
        for (idx_xstat = 0; idx_xstat < (cnt_xstats - 1); idx_xstat++) {
            if (xstats[idx_xstat].value)
                dprintf(fd, "%s=%"PRIu64",",
                        xstats_names[idx_xstat].name,
                        xstats[idx_xstat].value);
        }
        if (xstats[idx_xstat].value)
            dprintf(fd, "%s=%"PRIu64"\n",
                    xstats_names[idx_xstat].name,
                    xstats[idx_xstat].value);
        free(xstats_names);
        free(xstats);
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

int
show_version(int fd)
{

    dprintf(fd, "Natasha version: %s\n", GIT_VERSION);
    return 0;
}
