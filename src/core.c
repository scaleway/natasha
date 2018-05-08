#include <fcntl.h>
#include <unistd.h>

#include <rte_common.h>
#include <rte_cycles.h>
#include <rte_eal.h>
#include <rte_errno.h>
#include <rte_ethdev.h>
#include <rte_log.h>
#include <rte_mempool.h>
#include <rte_memzone.h>
#include <rte_prefetch.h>
#include <rte_version.h>

#include "natasha.h"

/* check DPDK version */
#if RTE_VER_YEAR != 17 || RTE_VER_MONTH != 11
#error The DPDK version you are using is not supported, please use DPDK v17.11
#endif

static int
dispatch_packet(struct rte_mbuf *pkt, uint8_t port, struct core *core)
{
    struct ether_hdr *eth_hdr;
    uint16_t eth_type;
    int status;

    eth_hdr = rte_pktmbuf_mtod(pkt, struct ether_hdr *);
    eth_type = eth_hdr->ether_type;

    status = -1;

    // We could convert eth_type to host byte order with
    //
    //      eth_type = rte_be_to_cpu_16(eth_type)
    //
    // and compare against little endian numbers, but the swap instruction is a
    // bottleneck: comparing eth_type against big endian numbers improves
    // performances a lot (+100 000 pps).
    //
    // We can't call rte_cpu_to_be_16() in a switch/case (compiler raises "case
    // label does not reduce to an integer constant"), hence the define.
    #if RTE_BYTE_ORDER == RTE_LITTLE_ENDIAN
    #define _htons(x) ((((x) & 0x00ffU) << 8) | (((x) & 0xff00U) >> 8))
    #else
    #define _htons(x) (x)
    #endif

    switch (eth_type) {
    case _htons(ETHER_TYPE_IPv4):
        status = ipv4_handle(pkt, port, core);
        break ;

    case _htons(ETHER_TYPE_ARP):
        status = arp_handle(pkt, port, core);
        break ;

    case _htons(ETHER_TYPE_IPv6):
    default:
        RTE_LOG(DEBUG, APP, "Unhandled proto %x on port %d\n", eth_type, port);
        break ;
    }

    if (status < 0) {
        rte_pktmbuf_free(pkt);
    }

    return 0;
}

/*
 * Read packets on port and call dispatch_packet for each of them.
 */
static int
handle_port(uint8_t port, struct core *core)
{
    struct rte_mbuf *pkts[32];
    uint16_t i;
    uint16_t nb_pkts;

    nb_pkts = rte_eth_rx_burst(port, core->rx_queues[port].id,
                               pkts, sizeof(pkts) / sizeof(*pkts));

    if (unlikely(nb_pkts == 0)) {
        return 0;
    }

    for (i = 0; i < nb_pkts - 1; ++i) {
        rte_prefetch0(rte_pktmbuf_mtod(pkts[i + 1], void *));
        dispatch_packet(pkts[i], port, core);
    }
    dispatch_packet(pkts[i], port, core);
    return i;
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

    eth_dev_count = rte_eth_dev_count();

    while (1) {
        // At any time, config.c/app_config_reload_all() can update
        // core->app_config to load a new configuration. The reload function
        // needs to free the old configuration, and for that it waits us to
        // mark the new configuration as used, which implies we no longer
        // reference the old config.
        core->app_config->used = 1;

        for (port = 0; port < eth_dev_count; ++port) {
            // Read and process incoming packets.
            handle_port(port, core);
        }

        for (port = 0; port < eth_dev_count; ++port) {
            // Write out packets.
            tx_flush(port, &core->tx_queues[port]);
        }
    }
    return 0;
}

/* Check the link status of all ports in up to 9s, and print them finally */
static void
check_ports_link_status(uint16_t port_max)
{
#define MAX_CHECK_TIME 90 /* 9s (90 * 100ms) in total */
#define CHECK_INTERVAL 100 /* 100ms */
    uint8_t count, all_ports_up;
    struct rte_eth_link link;
    uint16_t portid;

    RTE_LOG(INFO, APP, "Checking link status\n");
    for (count = 0; count <= MAX_CHECK_TIME; count++) {
        all_ports_up = 1;
        for (portid = 0; portid < port_max; portid++) {
            memset(&link, 0, sizeof(link));
            rte_eth_link_get_nowait(portid, &link);
            if (link.link_status)
                RTE_LOG(INFO, APP,
                        "Port%d Link Up. Speed %u Mbps - %s\n",
                        portid, link.link_speed,
                        (link.link_duplex == ETH_LINK_FULL_DUPLEX) ?
                        ("full-duplex") : ("half-duplex\n"));
            else
                RTE_LOG(INFO, APP, "Port %d not ready yet.\n", portid);

            /* clear all_ports_up flag if any link down */
            if (link.link_status == ETH_LINK_DOWN) {
                all_ports_up = 0;
                break;
            }
        }
        if (all_ports_up == 0) {
            RTE_LOG(INFO, APP, ".");
            rte_delay_ms(CHECK_INTERVAL);
        }

        /* set the print_flag if all ports up or timeout */
        if (all_ports_up == 1 || count == (MAX_CHECK_TIME - 1)) {
            RTE_LOG(INFO, APP, "Checking link status done\n");
            break;
        }
    }
}

static int
setup_queues(uint8_t port, struct core *cores, unsigned int ncores)
{
    char mempool_name[RTE_MEMZONE_NAMESIZE];
    static const int rx_ring_size = 256;
    static const int tx_ring_size = 512;
    struct rte_mempool *mempool;
    int per_queue_stats_enabled;
    uint16_t queue_id = 0;
    int rx_stats_idx;
    int tx_stats_idx;
    uint32_t core;
    int socket;
    int ret;

    per_queue_stats_enabled = support_per_queue_statistics(port);

    RTE_LCORE_FOREACH_SLAVE(core) {
        rx_stats_idx = queue_id;
        tx_stats_idx = queue_id + ncores - 1;

        // NUMA socket of this processor
        socket = rte_lcore_to_socket_id(core);

        snprintf(mempool_name, sizeof(mempool_name), "%u:%u", port, queue_id);

        mempool = rte_pktmbuf_pool_create(
            mempool_name,
            8192,                           // nb elements
            512,                            // cache size
            0,                              // priv size
            9216 + RTE_PKTMBUF_HEADROOM,    // data room size
            socket                          // socket id
        );
        if (!mempool) {
            RTE_LOG(ERR, APP, "Port %i: unable to create mempool: %s\n",
                    port, rte_strerror(rte_errno));
            return -1;
        }

        // RX queue
        ret = rte_eth_rx_queue_setup(port, queue_id, rx_ring_size, socket,
                                     NULL, mempool);
        if (ret < 0) {
            RTE_LOG(ERR, APP,
                    "Port %i: failed to setup RX queue %i on core %i: %s\n",
                    port, queue_id, core, rte_strerror(rte_errno));
            return ret;
        }

        if (per_queue_stats_enabled) {
            ret = rte_eth_dev_set_rx_queue_stats_mapping(port, queue_id,
                                                         rx_stats_idx);
            if (ret < 0) {
                RTE_LOG(ERR, APP,
                        "Port %i: failed to setup statistics of RX queue %i on "
                        "core %i: %s\n",
                        port, queue_id, core, rte_strerror(rte_errno));
                return ret;
            }
        }

        // TX queue
        ret = rte_eth_tx_queue_setup(port, queue_id, tx_ring_size, socket,
                                     NULL);
        if (ret < 0) {
            RTE_LOG(ERR, APP,
                    "Port %i: failed to setup TX queue %i on core %i: %s\n",
                    port, queue_id, core, rte_strerror(rte_errno));
            return ret;
        }

        if (per_queue_stats_enabled) {
            ret = rte_eth_dev_set_tx_queue_stats_mapping(port, queue_id,
                                                         tx_stats_idx);
            if (ret < 0) {
                RTE_LOG(ERR, APP,
                        "Port %i: failed to setup statistics of TX queue %i on "
                        "core %i: %s\n",
                        port, queue_id, core, rte_strerror(rte_errno));
                return ret;
            }
        }

        RTE_LOG(DEBUG, APP,
                "Port %i: RX/TX queues %i setup on core %i (socket: %i)\n",
                port, queue_id, core, socket);

        cores[core].rx_queues[port].id = queue_id;
        cores[core].tx_queues[port].id = queue_id;

        ++queue_id;
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
 *
 * DPDK initialization parameters documentation is available in
 * docs/DPDK_INITIALIZATION.md.
 */
static int
setup_port(uint8_t port, struct app_config *app_config, struct core *cores)
{
    int ret;

    struct rte_eth_dev_info dev_info;
    unsigned int ncores;
    uint16_t nqueues;
    struct rte_eth_conf eth_conf = {
        .link_speeds        = ETH_LINK_SPEED_AUTONEG,
        .rxmode = {
            .mq_mode        = ETH_MQ_RX_RSS,
            .jumbo_frame    = (app_config->ports[port].mtu > ETHER_MAX_LEN),
            .max_rx_pkt_len = app_config->ports[port].mtu,
            .header_split   = 0,
            .split_hdr_size = 0,
            .hw_ip_checksum = 0,
            .hw_vlan_filter = 1,
            .hw_vlan_strip  = 1,
            .hw_vlan_extend = 0,
            .hw_strip_crc   = 1,
            .enable_scatter = 0,
            .enable_lro     = 0,
        },
        .txmode = {
            .mq_mode                 = ETH_MQ_TX_NONE,
            .pvid                    = 0,
            .hw_vlan_reject_tagged   = 0,
            .hw_vlan_reject_untagged = 0,
            .hw_vlan_insert_pvid     = 0,
        },
        .lpbk_mode=0,
        .rx_adv_conf  =  {
            .rss_conf  =  {
                .rss_key     = NULL,
                .rss_key_len = 0,
                .rss_hf      = ETH_RSS_IP | ETH_RSS_TCP | ETH_RSS_UDP,
            },
        },
    };

    struct app_config_port_ip_addr *port_ip_addr;

    rte_eth_dev_info_get(port, &dev_info);
    if ((dev_info.tx_offload_capa & DEV_TX_OFFLOAD_IPV4_CKSUM) == 0 ||
        (dev_info.tx_offload_capa & DEV_TX_OFFLOAD_TCP_CKSUM) == 0 ||
        (dev_info.tx_offload_capa & DEV_TX_OFFLOAD_UDP_CKSUM) == 0
    ) {
        RTE_LOG(ERR, APP, "Port %i doesn't support IP, TCP or UDP checksum\n",
                port);
        return -1;
	}

    if (app_config->ports[port].ip_addresses == NULL) {
        RTE_LOG(ERR, APP, "Missing configuration for port %i\n", port);
        return -1;
    }

    ncores = rte_lcore_count();

    // One RX and one TX queue per core, except for the master core
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

    // Configure network queues
    ret = setup_queues(port, cores, ncores);
    if (ret < 0) {
        RTE_LOG(ERR, APP, "Port %i: unable to setup network queues\n", port);
        return ret;
    }

    // Start device
    ret = rte_eth_dev_start(port);
    if (ret < 0) {
        RTE_LOG(ERR, APP, "Port %i: unable to start device\n", port);
        return ret;
    }
    RTE_LOG(DEBUG, APP, "Port %i: started!\n", port);
    return 0;
}

/*
 * Call main_loop for each worker.
 */
static int
run_workers(struct core *cores)
{
    int ret;
    int core;

    RTE_LCORE_FOREACH_SLAVE(core) {
        ret = rte_eal_remote_launch(main_loop, &cores[core], core);
        if (ret < 0) {
            RTE_LOG(ERR, APP, "Cannot launch worker for core %i\n", core);
            return -1;
        }
    }
    return 0;
}

/*
 * Initialize Ethernet ports and workers.
 */
static int
setup_app(struct core *cores, int argc, char **argv)
{
    int ret;

    struct app_config *app_config;
    uint8_t port;
    uint8_t eth_dev_count;
    unsigned ncores;
    unsigned int core;

    // Parse configuration
    app_config = app_config_load(argc, argv, SOCKET_ID_ANY);
    if (app_config == NULL) {
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
                "Invalid coremask. The master core being used for the "
                "administration server, at least one other core needs "
                "to be activated for networking\n");
        return -1;
    }

    RTE_LOG(INFO, APP, "Using %i ethernet devices\n", eth_dev_count);
    RTE_LOG(INFO, APP, "Using %i logical cores\n", ncores);

    // Configure ports
    for (port = 0; port < eth_dev_count; ++port) {
        RTE_LOG(INFO, APP, "Configuring port %i...\n", port);
        ret = setup_port(port, app_config, cores);
        if (ret < 0) {
            RTE_LOG(ERR, APP, "Cannot initialize network ports\n");
            return -1;
        }
    }

    check_ports_link_status(eth_dev_count);

    // Configuration for the master core is only used to setup ports.
    app_config_free(app_config);

    // Initialize workers
    RTE_LCORE_FOREACH_SLAVE(core) {
        cores[core].id = core;
        memset(&cores[core].app_config, 0, sizeof(cores[core].app_config));
    }

    // Load the configuration for each worker
    if (app_config_reload_all(cores, argc, argv, STDOUT_FILENO) < 0) {
        return -1;
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
    int flags;

    // RTE_LOG logs on stderr. If stderr is a pipe (for instance in the case
    // natasha is managed by supervisord) and the pipe is full because no one
    // is reading on the other end (which happens if supervisor's reader
    // crashes — which happens —), the logging will become blocking and the app
    // will be stuck.
    // In this case, we prefer to lose logging rather than having a non-working
    // application.
    if ((flags = fcntl(STDERR_FILENO, F_GETFL, 0)) < 0 ||
        fcntl(STDERR_FILENO, F_SETFL, flags | O_NONBLOCK) < 0) {

        rte_exit(EXIT_FAILURE, "Unable to set stderr as non-blocking\n");
    }

    ret = rte_eal_init(argc, argv);
    if (ret < 0) {
        rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");
    }
    argc -= ret;
    argv += ret;

    if (setup_app(cores, argc, argv) < 0) {
        rte_exit(EXIT_FAILURE, "Unable to setup app\n");
    }

    if (run_workers(cores) < 0) {
        rte_exit(EXIT_FAILURE, "Unable to run workers\n");
    }

    return adm_server(cores, argc, argv);
}
