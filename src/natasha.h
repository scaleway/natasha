#ifndef CORE_H_
#define CORE_H_

#include <rte_ethdev.h>
#include <rte_rwlock.h>
#include <rte_malloc.h>

/*
 * Logging configuration.
 */
#ifdef DEBUG
    #define LOG_LEVEL RTE_LOG_DEBUG
    #define LOG_DEBUG(log_type, fmt, args...) do {  \
        RTE_LOG(DEBUG, log_type, fmt, ##args);      \
    } while (0)
#else
    #define LOG_LEVEL RTE_LOG_INFO
    #define LOG_DEBUG(log_type, fmt, args...) do{} while(0)
#endif

// Configure log level type "APP": RTE_LOG(level, >> APP <<).
#define RTE_LOGTYPE_APP RTE_LOGTYPE_USER1


/*
 * Application configuration.
 */

// Forward declaration. Defined under "Workers and queues configuration".
struct core;

/*
 * Structure for nat related statistics
 * These stats SHOULD be kept per core.
 */
struct nat_stats {
    uint64_t drop_no_rule;              /* the most used stat */
    uint32_t drop_nat_condition;
    uint32_t drop_bad_l3_cksum;
    uint32_t rx_bad_l4_cksum;
    uint32_t drop_unknown_icmp;
    uint32_t drop_unhandled_ethertype;
    uint32_t drop_tx_notsent;
};

// Network port.
struct ip_vlan {
    uint32_t ip;
    uint16_t vlan;
};

struct port_ip_addr {
    struct ip_vlan addr;
    struct port_ip_addr *next;
};

struct port_config {
    struct port_ip_addr *ip_addresses;
    int mtu;
};

// A condition, to specify whether an action should be processed or not.
struct app_config_rule_cond {
    int (*f)(struct rte_mbuf *pkt,
             uint8_t port,
             struct core *core,
             void *data);
    void *params;
};

// See docs/CONFIGURATION.md.
struct app_config_node {
    struct app_config_node *left;
    struct app_config_node *right;

    enum {
        NOOP, // Unused
        ACTION, // Execute the action
        SEQ, // Execute both left and right
        IF, // Execute right if left is false
        COND, // Execute right if left is true
        AND, // True if left and right are true
        OR, // True if left or right is true
    } type;

    int (*action)(struct rte_mbuf *pkt, uint8_t port, struct core *core,
                  void *data);
    void *data;
};

// Software configuration.
/*
 * Size of the first, second and third row of the NAT lookup table.
 */
static const int lkp_fs = 256; // 2^8
static const int lkp_ss = 256; // 2^8
static const int lkp_ts = 65536; // 2^16

struct nat_address {
    uint64_t bytes;
    uint32_t address;
};

#define NATASHA_MAX_ETHPORTS    2
struct app_config {
    struct port_config ports[NATASHA_MAX_ETHPORTS];

    /*
     * Contain NAT rules. The rule "10.1.2.3 -> 212.10.11.12" is stored as
     * two entries, as follow:
     *
     * - nat_lookup = table of 256 (2^8) int **
     * - nat_lookup[10] = table of 256 (2^8) int *
     * - nat_lookup[10][1] = table of 65536 (2^16) int
     * - nat_lookup[10][1][2 << 16 & 3] = 212.10.11.12
     *
     * and:
     *
     * - nat_lookup = table of 256 (2^8) int **
     * - nat_lookup[212] = table of 256 (2^8) int *
     * - nat_lookup[212][10] = table of 65536 (2^16) int
     * - nat_lookup[212][10][11 << 16 & 12] = 10.1.2.3
     */
    uint32_t ***nat_lookup;

    struct app_config_node *rules;

    /* NATASHA flags */
#define NAT_FLAG_USED           0x0001  /* If NAT_FLAG_USED, this configuration
                                         * has been used at least once and in
                                         * case of reload (see config.c/app_config_relaod_all),
                                         * old configuration is no longer used
                                         * by the core and can safely be freed.
                                         */
    volatile uint32_t flags;

} __rte_cache_aligned;


/*
 * Workers and queues configuration.
 */

// Network receive queue.
struct rx_queue {
    uint16_t id;
};

#define MAX_TX_BURST 32
// Network transmit queue.
struct tx_queue {
    // Packets to send.
    struct rte_mbuf *pkts[MAX_TX_BURST];
    uint16_t id;
    // Number of packets in pkts.
    uint16_t len;
};

#define NATASHA_MAX_QUEUES    16
// A core and its queues. Each core has one rx queue and one tx queue per port.
struct core {
    struct app_config *app_config;
    struct rx_queue rx_queues[NATASHA_MAX_QUEUES];
    struct tx_queue tx_queues[NATASHA_MAX_QUEUES];
    struct nat_stats *stats;
    uint32_t id;
} __rte_cache_aligned;

/*
 * Prototypes.
 */

// config.c
struct app_config *app_config_load(int argc, char **argv,
                                   unsigned int socket_id);
void app_config_free(struct app_config *config);
int support_per_queue_statistics(uint8_t port);
int app_config_reload_all(struct core *cores, int argc, char **argv,
                          int out_fd);

// stats.c
void stats_display(int fd);
void xstats_display(int fd, struct core *cores);
int stats_reset(int fd);

// pkt.c
uint16_t tx_send(struct rte_mbuf *pkt, uint8_t port, struct tx_queue *queue,
                  struct nat_stats *stats);
uint16_t tx_flush(uint8_t port, struct tx_queue *queue,
                  struct nat_stats *stats);

int is_natasha_ip(struct app_config *app_config,
                  uint32_t ip, int vlan);
int is_natasha_port_ip(struct app_config *app_config,
                       uint32_t ip, int vlan, uint8_t port);

// arp.c
int arp_handle(struct rte_mbuf *pkt, uint8_t port, struct core *core);
int ipv4_handle(struct rte_mbuf *pkt, uint8_t port, struct core *core);

// adm.c
int adm_server(struct core *cores, int argc, char **argv);

/*
 * Utility macros.
 */
#define IPv4_FMT            "%i.%i.%i.%i"
#define IPv4_FMTARGS(ip)    ((ip) >> 24) & 0xff,   \
                            ((ip) >> 16) & 0xff,   \
                            ((ip) >>  8) & 0xff,    \
                            ((ip) >>  0) & 0xff

#define MAC_FMT                 "%x:%x:%x:%x:%x:%x"
#define MAC_FMTARGS(ether_addr) (ether_addr).addr_bytes[0], \
                                (ether_addr).addr_bytes[1], \
                                (ether_addr).addr_bytes[2], \
                                (ether_addr).addr_bytes[3], \
                                (ether_addr).addr_bytes[4], \
                                (ether_addr).addr_bytes[5]

#endif
