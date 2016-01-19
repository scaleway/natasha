#include <signal.h>

#include <rte_common.h>
#include <rte_cycles.h>
#include <rte_eal.h>
#include <rte_errno.h>
#include <rte_ethdev.h>
#include <rte_log.h>
#include <rte_mempool.h>

#include "adm.h"
#include "natasha.h"


// Flush TX queues after ~BURST_TX_DRAIN_US microseconds.
static const int
BURST_TX_DRAIN_US = 1000; // 0.1ms


static int
dispatch_packet(struct rte_mbuf *pkt, uint8_t port, struct core *core)
{
    struct ether_hdr *eth_hdr;
    uint16_t eth_type;
    int status;

    eth_hdr = rte_pktmbuf_mtod(pkt, struct ether_hdr *);
    eth_type = rte_be_to_cpu_16(eth_hdr->ether_type);

    status = -1;

    switch (eth_type) {

    case ETHER_TYPE_ARP:
        status = arp_handle(pkt, port, core);
        break ;

    case ETHER_TYPE_IPv4:
        status = ipv4_handle(pkt, port, core);
        break ;

    case ETHER_TYPE_IPv6:
    default:
        RTE_LOG(DEBUG, APP, "Unhandled proto %x on port %d\n", eth_type, port);
        break ;
    }

    if (status < 0) {
        rte_pktmbuf_free(pkt);
    }

    return 0;
}

static int
handle_port(uint8_t port, struct core *core)
{
    struct rte_mbuf *pkts[64];
    uint16_t i;
    uint16_t nb_pkts;

    nb_pkts = rte_eth_rx_burst(port, core->rx_queues[port].id,
                               pkts, sizeof(pkts) / sizeof(*pkts));

    for (i = 0; i < nb_pkts; ++i) {
        dispatch_packet(pkts[i], port, core);
    }
    return 0;
}

int
reload_conf(struct core *core)
{
    int ret;

    ret = app_config_reload(&core->app_config, core->app_argc, core->app_argv);
    if (ret < 0) {
        RTE_LOG(CRIT, APP, "Core %i: unable to load configuration\n",
                core->id);
    } else {
        core->need_reload_conf = 0;
        RTE_LOG(INFO, APP, "Core %i: configuration reloaded!\n", core->id);
    }
    return ret;
}

/*
 * Main loop, executed by every core except the master.
 */
static int
main_loop(void *pcore)
{
    uint8_t port;
    uint8_t eth_dev_count;
    struct core *core = pcore;

    uint64_t prev_tsc;
    const uint64_t drain_tsc =
        (rte_get_tsc_hz() + US_PER_S - 1) / US_PER_S * BURST_TX_DRAIN_US;

    eth_dev_count = rte_eth_dev_count();
    prev_tsc = rte_rdtsc();

    while (1) {
        const uint64_t cur_tsc = rte_rdtsc();
        int need_flush;

        if (core->need_reload_conf) {
            if (reload_conf(core) < 0) {
                return -1;
            }
        }

        // We need to flush if the last flush occured more than drain_tsc ago.
        need_flush = (cur_tsc - prev_tsc > drain_tsc) ? 1 : 0;
        if (need_flush) {
            prev_tsc = cur_tsc;
        }

        for (port = 0; port < eth_dev_count; ++port) {
            handle_port(port, core);

            if (need_flush) {
                tx_flush(port, &core->tx_queues[port]);
            }

        }
    }
    return 0;
}

/*
 * Initialize a network port and its network queues.
 *
 * For each logical non-master core activated, we setup a RX and a TX queue. We
 * also collect queues statistics at indexes <lcore idx> for the RX queue, and
 * <lcore idx + number of cores> for the TX queue.
 *
 * Example with the slave cores 3, 4 and 5 activated:
 *
 * Core 3: RX Queue 0 (stats idx=0), TX Queue 0 (stats idx=3)
 * Core 4: RX Queue 1 (stats idx=1), TX Queue 1 (stats idx=4)
 * Core 5: RX Queue 2 (stats idx=2), TX Queue 2 (stats idx=5)
 */
static int
port_init(uint8_t port, struct app_config *app_config, struct core *cores)
{
    int ret;

    unsigned int ncores;
    uint16_t nqueues;
    static const struct rte_eth_conf eth_conf = {
        .rxmode = {
            .split_hdr_size = 0,
            .header_split = 0,           /* Header Split disabled */
            .hw_ip_checksum = 1,         /* IP checksum offload enabled */

            // Enable VLAN filtering to only accept traffic from VLANs
            // configured with rte_eth_dev_vlan_filter.
            .hw_vlan_filter = 1,

            .max_rx_pkt_len = 9198,
            .jumbo_frame = 1,            /* Jumbo Frame Support disabled */
            .hw_strip_crc = 0,           /* CRC stripped by hardware */
            .mq_mode = ETH_MQ_RX_RSS,
        },
        .rx_adv_conf = {
            .rss_conf = {
                .rss_key = NULL,
                .rss_hf = ETH_RSS_IPV4
                            | ETH_RSS_NONFRAG_IPV4_TCP
                            | ETH_RSS_NONFRAG_IPV4_UDP,
            },
        },
        .txmode = {
            .mq_mode = ETH_MQ_TX_NONE,
        },
    };
    unsigned int core;
    uint16_t queue_id;
    struct app_config_port_ip_addr *port_ip_addr;

    /* Configure device */
    ncores = rte_lcore_count();
    // one RX and one TX queue per core, except for the master core
    nqueues = ncores - 1;

    ret = rte_eth_dev_configure(port, nqueues, nqueues, &eth_conf);
    if (ret < 0) {
        RTE_LOG(ERR, APP, "Failed to configure ethernet device port %i\n",
                port);
        return ret;
    }

    // Accept traffic for VLANs
    port_ip_addr = app_config->ports[port].ip_addresses;
    while (port_ip_addr) {

        if (port_ip_addr->addr.vlan &&
            rte_eth_dev_vlan_filter(port, port_ip_addr->addr.vlan, 1) < 0) {

            RTE_LOG(ERR, APP,
                    "Failed to filter traffic on vlan %i for port %i\n",
                    port_ip_addr->addr.vlan, port);
            return -1;
        }

        port_ip_addr = port_ip_addr->next;
    }

    // ETH_VLAN_STRIP_OFFLOAD: remove VLAN header
    // ETH_VLAN_FILTER_OFFLOAD: accept traffic from VLANs configured above
    if (rte_eth_dev_set_vlan_offload(
        port, ETH_VLAN_STRIP_OFFLOAD | ETH_VLAN_FILTER_OFFLOAD
    ) < 0) {
        RTE_LOG(ERR, APP, "Failed to setup vlan offload features on port %i\n",
                port);
        return -1;
    }

    /* Configure queues */
    queue_id = 0;
    RTE_LCORE_FOREACH_SLAVE(core) {
        static const int rx_ring_size = 128;
        static const int tx_ring_size = 256;
        static const struct rte_eth_rxconf rx_conf = {
            .rx_thresh = {
                .pthresh = 8,       // Ring prefetch threshold.
                .hthresh = 8,       // Ring host threshold.
                .wthresh = 4,       // Ring writeback threshold.
            },
            .rx_free_thresh = 64,   // Drives the freeing of RX descriptors.

            // Drop packets if no descriptors are available.
            .rx_drop_en = 1,
            // Do not start queue with rte_eth_dev_start().
            .rx_deferred_start = 0,
        };
        static const struct rte_eth_txconf tx_conf = {
            .tx_thresh = {
                .pthresh = 36,  // Ring prefetch threshold.
                .hthresh = 0,   // Ring host threshold.
                .wthresh = 0,   // Ring writeback threshold.
            },
            .tx_rs_thresh = 0,  // Drives the setting of RS bit on TXDs.

            // Start freeing TX buffers if there are less free descriptors than
            // this value.
            .tx_free_thresh = 0,
            // Set flags for the Tx queue.
            .txq_flags = 0,
            // Do not start queue with rte_eth_dev_start().
            .tx_deferred_start = 0,
        };

        struct rte_mempool *mempool;
        char mempool_name[RTE_MEMPOOL_NAMESIZE];
        int socket;
        const int rx_stats_idx = queue_id;
        const int tx_stats_idx = queue_id + ncores - 1;

        snprintf(mempool_name, sizeof(mempool_name),
                 "core_%d_port_%d", core, port);

        // NUMA socket of this processor
        socket = rte_lcore_to_socket_id(core);

        mempool = rte_mempool_create(
            mempool_name,
            /* n, number of elements in the pool – optimum size is 2^q-1 */
            1023,
            /* size of each element */
            9198 + sizeof(struct rte_mbuf) + RTE_PKTMBUF_HEADROOM,
            /* cache size – must be below CONFIG_RTE_MEMPOOL_CACHE_MAX_SIZE and
             * below (n / 1.5). (n % cache size) should be equal to 0. */
            0,
            /* private data size */
            sizeof(struct rte_pktmbuf_pool_private),
            /* mempool init and mempool init arg */
            rte_pktmbuf_pool_init, NULL,
            /* obj init and obj init arg */
            rte_pktmbuf_init, NULL,
            /* NUMA socket */
            socket,
            /* flags */
            0
        );
        if (!mempool) {
            RTE_LOG(ERR, APP, "Port %i: unable to create mempool: %s\n",
                    port, rte_strerror(rte_errno));
            return -1;
        }

        // RX queue
        ret = rte_eth_rx_queue_setup(port, queue_id, rx_ring_size, socket,
                                     &rx_conf, mempool);
        if (ret < 0) {
            RTE_LOG(ERR, APP,
                    "Port %i: failed to setup RX queue %i on core %i: %s\n",
                    port, queue_id, core, rte_strerror(rte_errno));
            return ret;
        }

        ret = rte_eth_dev_set_rx_queue_stats_mapping(port, queue_id,
                                                     rx_stats_idx);
        if (ret < 0) {
            RTE_LOG(ERR, APP,
                    "Port %i: failed to setup statistics of RX queue %i on "
                    "core %i: %s\n",
                    port, queue_id, core, rte_strerror(rte_errno));
            return ret;
        }

        // TX queue
        ret = rte_eth_tx_queue_setup(port, queue_id, tx_ring_size, socket,
                                     &tx_conf);
        if (ret < 0) {
            RTE_LOG(ERR, APP,
                    "Port %i: failed to setup TX queue %i on core %i: %s\n",
                    port, queue_id, core, rte_strerror(rte_errno));
            return ret;
        }

        ret = rte_eth_dev_set_tx_queue_stats_mapping(port, queue_id,
                                                     tx_stats_idx);
        if (ret < 0) {
            RTE_LOG(ERR, APP,
                    "Port %i: failed to setup statistics of TX queue %i on "
                    "core %i: %s\n",
                    port, queue_id, core, rte_strerror(rte_errno));
            return ret;
        }

        RTE_LOG(DEBUG, APP,
                "Port %i: RX/TX queues %i setup on core %i (socket: %i)\n",
                port, queue_id, core, socket);

        cores[core].rx_queues[port].id = queue_id;
        cores[core].tx_queues[port].id = queue_id;

        ++queue_id;
    }

    /* Start device */
    ret = rte_eth_dev_start(port);
    if (ret < 0) {
        RTE_LOG(ERR, APP, "Port %i: unable to start device\n", port);
        return ret;
    }
    RTE_LOG(DEBUG, APP, "Port %i: started!\n", port);
    return 0;
}

/*
 * Setup ethernet devices, initialize cores and run workers.
 */
static int
run_workers(struct core *cores, int argc, char **argv)
{
    int ret;

    struct app_config app_config = {};
    uint8_t port;
    uint8_t eth_dev_count;
    unsigned ncores;
    unsigned int core;

    // Parse configuration
    if (app_config_reload(&app_config, argc, argv) < 0) {
        RTE_LOG(ERR, APP, "Unable to load configuration\n");
        return -1;
    }

    eth_dev_count = rte_eth_dev_count();
    if (eth_dev_count == 0) {
        RTE_LOG(ERR, APP, "No network device using DPDK-compatible driver\n");
        return -1;
    }

    ncores = rte_lcore_count();
    if (ncores < 2) {
        RTE_LOG(ERR, APP,
                "Invalid coremask. The master core being used for statistics, "
                "at least one other core needs to be activated for "
                "networking\n");
        return -1;
    }

    RTE_LOG(INFO, APP, "Using %i ethernet devices\n", eth_dev_count);
    RTE_LOG(INFO, APP, "Using %i logical cores\n", ncores);

    // Configure ports
    for (port = 0; port < eth_dev_count; ++port) {
        RTE_LOG(INFO, APP, "Configuring port %i...\n", port);
        ret = port_init(port, &app_config, cores);
        if (ret < 0) {
            RTE_LOG(ERR, APP, "Cannot initialize network ports\n");
            return -1;
        }
    }

    // Configuration for the master core is only used to setup ports.
    app_config_free(&app_config);

    // Launch workers
    RTE_LCORE_FOREACH_SLAVE(core) {
        cores[core].id = core;
        cores[core].app_argc = argc;
        cores[core].app_argv = argv;
        cores[core].need_reload_conf = 1;

        ret = rte_eal_remote_launch(main_loop, &cores[core], core);
        if (ret < 0) {
            RTE_LOG(ERR, APP, "Cannot launch worker for core %i\n", core);
            return -1;
        }
    }

    return 0;
}

int
#ifndef UNITTEST
main(int argc, char **argv)
#else
natasha(int argc, char **argv)
#endif
{
    int ret;
    struct core cores[RTE_MAX_LCORE] = {};

    ret = rte_eal_init(argc, argv);
    if (ret < 0) {
		rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");
    }
    argc -= ret;
    argv += ret;

    ret = run_workers(cores, argc, argv);
    if (ret < 0) {
        rte_exit(EXIT_FAILURE, "Unable to launch workers\n");
    }

    // If we write to a CLI client when he is disconnected, write() will return
    // -1 instead of raising SIGPIPE.
    signal(SIGPIPE, SIG_IGN);

    return adm_server(cores, argc, argv);
}
