/* vim: ts=4 sw=4 et */
#ifndef CLI_H_
#define CLI_H_

#include <stdint.h>

/*
 * This file contains the data structures used by the CLI
 * This data structures are shared with the CLI client bin (written in golang)
 * thus, only necessary data strucutures are put in this file.
 *
 */

enum natasha_cmd_type {
    NATASHA_CMD_NONE,
    NATASHA_CMD_STATUS,
    NATASHA_CMD_EXIT,
    NATASHA_CMD_RELOAD,
    NATASHA_CMD_RESET_STATS,
    NATASHA_CMD_DPDK_STATS,
    NATASHA_CMD_DPDK_XSTATS,
    NATASHA_CMD_APP_STATS,
    NATASHA_CMD_VERSION,
};

#define NATASHA_REPLY_OK     0

struct natasha_cmd_reply {
    uint8_t     type;
    uint8_t     status;
    uint16_t    data_size;
};

char natasha_version[100];

/*
 * Structure for nat related statistics
 * These stats SHOULD be kept per core.
 */
struct natasha_app_stats {
    uint64_t drop_no_rule;              /* the most used stat */
    uint32_t drop_nat_condition;
    uint32_t drop_bad_l3_cksum;
    uint32_t rx_bad_l4_cksum;
    uint32_t drop_unknown_icmp;
    uint32_t drop_unhandled_ethertype;
    uint32_t drop_tx_notsent;
};

/* Structures and definition retreived from DPDK 18.02.2 stable */

#define RTE_ETHDEV_QUEUE_STAT_CNTRS 16
/*
 * A structure used to retrieve statistics for an Ethernet port.
 * Not all statistics fields in struct rte_eth_stats are supported
 * by any type of network interface card (NIC). If any statistics
 * field is not supported, its value is 0.
 */
struct natasha_eth_stats {
	uint64_t ipackets;  /**< Total number of successfully received packets. */
	uint64_t opackets;  /**< Total number of successfully transmitted packets.*/
	uint64_t ibytes;    /**< Total number of successfully received bytes. */
	uint64_t obytes;    /**< Total number of successfully transmitted bytes. */
	uint64_t imissed;
	/**< Total of RX packets dropped by the HW,
	 * because there are no available buffer (i.e. RX queues are full).
	 */
	uint64_t ierrors;   /**< Total number of erroneous received packets. */
	uint64_t oerrors;   /**< Total number of failed transmitted packets. */
	uint64_t rx_nombuf; /**< Total number of RX mbuf allocation failures. */
	uint64_t q_ipackets[RTE_ETHDEV_QUEUE_STAT_CNTRS];
	/**< Total number of queue RX packets. */
	uint64_t q_opackets[RTE_ETHDEV_QUEUE_STAT_CNTRS];
	/**< Total number of queue TX packets. */
	uint64_t q_ibytes[RTE_ETHDEV_QUEUE_STAT_CNTRS];
	/**< Total number of successfully received queue bytes. */
	uint64_t q_obytes[RTE_ETHDEV_QUEUE_STAT_CNTRS];
	/**< Total number of successfully transmitted queue bytes. */
	uint64_t q_errors[RTE_ETHDEV_QUEUE_STAT_CNTRS];
	/**< Total number of queue packets received that are dropped. */
};


#endif
