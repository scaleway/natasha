#ifndef CORE_H_
#define CORE_H_

#include <rte_ethdev.h>

/*
 * Logging configuration.
 */
#ifdef DEBUG
    #define LOG_LEVEL RTE_LOG_DEBUG
    #define LOG_DEBUG(log_type, fmt, args...) do {	\
        RTE_LOG(DEBUG, log_type, fmt, ##args);		\
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

// Network port.
struct app_config_port {
    uint32_t ip;
};

// A condition, to specify whether an action should be processed or not.
struct app_config_rule_cond {
    int (*f)(struct rte_mbuf *pkt,
             uint8_t port,
             struct core *core,
             void *data);
    void *params;
};

// Define what to do when an action is processed.
typedef enum {
    ACTION_NEXT,
    ACTION_BREAK,
} RULE_ACTION;

// An action to transform and/or send a packet.
struct app_config_rule_action {
    RULE_ACTION (*f)(struct rte_mbuf *pkt,
                     uint8_t port,
                     struct core *core,
                     void *data);
    void *params;
};

// Execute actions if only_if returns true.
struct app_config_rule {
    struct app_config_rule_cond only_if;
    struct app_config_rule_action actions[16]; // max 16 actions per rule
};

// Software configuration.
struct app_config {
    struct app_config_port ports[RTE_MAX_ETHPORTS];
    struct app_config_rule rules[64]; // max 64 rules
};


/*
 * Workers and queues configuration.
 */

// Network receive queue.
struct rx_queue {
    uint16_t id;
};

#define MAX_TX_BURST 64
// Network transmit queue.
struct tx_queue {
    uint16_t id;

    // Packets to send.
    struct rte_mbuf *pkts[MAX_TX_BURST];

    // Number of packets in pkts.
    uint16_t len;
};

// A core and its queues. Each core has one rx queue and one tx queue per port.
struct core {
    int id;

    int app_argc;
    char **app_argv;
    struct app_config app_config;

    // true if configuration must be reloaded
    int need_reload_conf;

    struct rx_queue rx_queues[RTE_MAX_ETHPORTS];
    struct tx_queue tx_queues[RTE_MAX_ETHPORTS];
};

/*
 * Prototypes.
 */
int app_config_init(struct app_config *config);
int app_config_load(struct app_config *config, int argc, char **argv);

uint16_t tx_send(struct rte_mbuf *pkt, uint8_t port, struct tx_queue *queue);
uint16_t tx_flush(uint8_t port, struct tx_queue *queue);

int arp_handle(struct rte_mbuf *pkt, uint8_t port, struct core *core);
int ipv4_handle(struct rte_mbuf *pkt, uint8_t port, struct core *core);


// Rule actions
RULE_ACTION action_print(struct rte_mbuf *pkt, uint8_t port, struct core *core,
                         void *data);


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
