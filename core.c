#include <stdio.h>
#include <unistd.h>

#include <rte_common.h>
#include <rte_eal.h>
#include <rte_errno.h>
#include <rte_ethdev.h>
#include <rte_log.h>
#include <rte_mempool.h>

#include "core.h"


/*
 * Each core has a RX and a TX queue on each ethernet device.
 */
struct core {
    uint16_t rx_queues[RTE_MAX_ETHPORTS];
    uint16_t tx_queues[RTE_MAX_ETHPORTS];
};

/*
 * Main loop, executed by every core except the master.
 */
static int
main_loop(void *pcore)
{
    struct core *core = pcore;

    return 0;
}

/*
 * Get or create a mempool.
 */
static struct rte_mempool *
get_or_create_mempool(const char *name,
                      unsigned n, unsigned elt_size,
                      unsigned cache_size, unsigned private_data_size,
                      rte_mempool_ctor_t *mp_init, void *mp_init_arg,
                      rte_mempool_obj_ctor_t *obj_init, void *obj_init_arg,
                      int socket_id, unsigned flags)
{
    struct rte_mempool *ret;

    ret = rte_mempool_lookup(name);
    if (ret) {
        return ret;
    }
    RTE_LOG(DEBUG, APP, "Creating mempool `%s'\n", name);
    return rte_mempool_create(
        name, n, elt_size,
        cache_size, private_data_size,
        mp_init, mp_init_arg,
        obj_init, obj_init_arg,
        socket_id, flags
    );
}

/*
 * Initialize network port, and create a RX and a TX queue for each logical
 * port activated – except the master core.
 */
static int
port_init(uint8_t port, struct core *cores)
{
    int ret;

    unsigned int ncores;
    uint16_t nqueues;
    static const struct rte_eth_conf eth_conf = {
        .rxmode = {
            .split_hdr_size = 0,
            .header_split = 0,              /* Header Split disabled */
               .hw_ip_checksum = 1,         /* IP checksum offload enabled */
               .hw_vlan_filter = 0,         /* VLAN filtering disabled */
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

        socket = rte_lcore_to_socket_id(core);
        snprintf(mempool_name, sizeof(mempool_name), "mempool_socket_%d",
                 socket);
        mempool = get_or_create_mempool(
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

        // TX queue
        ret = rte_eth_tx_queue_setup(port, queue_id, tx_ring_size, socket,
                                     &tx_conf);
        if (ret < 0) {
            RTE_LOG(ERR, APP,
                    "Port %i: failed to setup TX queue %i on core %i: %s\n",
                    port, queue_id, core, rte_strerror(rte_errno));
            return ret;
        }

        RTE_LOG(DEBUG, APP,
                "Port %i: RX/TX queues %i setup on core %i (socket: %i)\n",
                port, queue_id, core, socket);

        cores[core].rx_queues[port] = queue_id;
        cores[core].tx_queues[port] = queue_id;

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
 * Setup ethernet devices and run workers.
 */
static int
run_workers(void)
{
    int ret;

    uint8_t port;
    uint8_t eth_dev_count;
    unsigned ncores;
    unsigned int core;
    struct core cores[RTE_MAX_LCORE];

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
        ret = port_init(port, cores);
        if (ret < 0) {
            RTE_LOG(ERR, APP, "Cannot initialize network ports\n");
            return -1;
        }
        RTE_LOG(INFO, APP, "Port %i configured!\n", port);
    }

    RTE_LCORE_FOREACH_SLAVE(core) {
        ret = rte_eal_remote_launch(main_loop, &cores[core], core);
        if (ret < 0) {
            RTE_LOG(ERR,  APP, "Cannot launch worker for core %i\n", core);
            return -1;
        }
    }

    return 0;
}

int
MAIN(int argc, char **argv)
{
    int ret;

    ret = rte_eal_init(argc, argv);
    if (ret < 0) {
		rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");
    }
    argc -= ret;
    argv += ret;

    ret = run_workers();
    if (ret < 0) {
        rte_exit(EXIT_FAILURE, "Unable to launch workers\n");
    }

    while (1) {
        pause();
    }

    return EXIT_SUCCESS;
}
