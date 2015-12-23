#include <rte_ethdev.h>

#include "natasha.h"
#include "network_headers.h"


/*
 * Reply to a ICMP echo query.
 */
static int
icmp_echo(struct rte_mbuf *pkt, uint8_t port, struct core *core)
{
    struct ether_hdr *eth_hdr;
    struct ipv4_hdr *ipv4_hdr;
    struct icmp_hdr *icmp_hdr;
    struct ether_addr tmp_eth;
    uint32_t tmp_ip;

    icmp_hdr = icmp_header(pkt);
    icmp_hdr->icmp_type = IP_ICMP_ECHO_REPLY;

    // Swap ethernet addresses
    eth_hdr = eth_header(pkt);
    ether_addr_copy(&eth_hdr->s_addr, &tmp_eth);
    ether_addr_copy(&eth_hdr->d_addr, &eth_hdr->s_addr);
    ether_addr_copy(&tmp_eth, &eth_hdr->d_addr);

    // Swap IP addresses
    ipv4_hdr = ipv4_header(pkt);
    tmp_ip = ipv4_hdr->src_addr;
    ipv4_hdr->src_addr = ipv4_hdr->dst_addr;
    ipv4_hdr->dst_addr = tmp_ip;

    // Offload IPV4 checksum computation
    ipv4_hdr->hdr_checksum = 0;
    pkt->ol_flags |= PKT_TX_IP_CKSUM;

    // Compute ICMP checksum, RFC 1071
    icmp_hdr->icmp_cksum = 0;
    icmp_hdr->icmp_cksum = ~rte_raw_cksum(
        icmp_hdr,
        rte_be_to_cpu_16(ipv4_hdr->total_length) - sizeof(*ipv4_hdr)
    );

    return tx_send(pkt, port, &core->tx_queues[port]);
}

/*
 * Answer to an ICMP packet depending on its type.
 *
 * @return
 *  - -1 if processing this type is not yet implemented.
 */
static int
icmp_dispatch(struct rte_mbuf *pkt, uint8_t port, struct core *core)
{
    struct icmp_hdr *icmp_hdr;

    icmp_hdr = icmp_header(pkt);

    switch (icmp_hdr->icmp_type) {

    case IP_ICMP_ECHO_REQUEST:
        return icmp_echo(pkt, port, core);

    default:
        break ;
    }
    return -1;
}

/*
 * Call icmp_dispatch() if pkt is addressed to one of our interfaces.
 *
 * @return
 *  - -1 if pkt is not addresses to one of our interfaces.
 *  - The return value of icmp_dispatch() otherwise.
 */
static int
icmp_answer(struct rte_mbuf *pkt, uint8_t port, struct core *core)
{
    int ret;
    struct ipv4_hdr *ipv4_hdr;
    uint32_t dst_ip;
    uint8_t n;

    ipv4_hdr = ipv4_header(pkt);
    dst_ip = rte_be_to_cpu_32(ipv4_hdr->dst_addr);

    for (n = 0; n < RTE_MAX_ETHPORTS; ++n) {

        if (dst_ip == core->app_config.ports[n].ip) {
            ret = icmp_dispatch(pkt, port, core);
            // Even if we can't handle pkt, it is addressed to us. Drop it and
            // return 0 to mark it as processed.
            if (ret < 0) {
                rte_pktmbuf_free(pkt);
                return 0;
            }
            return ret;
        }
    }
    return -1;
}

#define X(cond) do {    \
    if ((cond) < 0) {   \
        return -1;      \
    }                   \
} while (0)

static int
process_rules(struct app_config_node *node, struct rte_mbuf *pkt, uint8_t port,
              struct core *core)
{
    int ret;

    if (!node) {
        return 0;
    }

    switch (node->type) {

    // Execute node's action.
    case ACTION:
        return node->action(pkt, port, core, node->data);

    // Execute left and right parts.
    case SEQ:
        X(process_rules(node->left, pkt, port, core));
        X(process_rules(node->right, pkt, port, core));
        return 0;

    // The left part of a IF node is a COND node, the right part is the else
    // clause. Only execute the else clause if the COND is false.
    case IF:
        X(ret = process_rules(node->left, pkt, port, core));
        if (ret == 0) {
            X(process_rules(node->right, pkt, port, core));
        }
        return 0;

    // The left part of a COND node is an ACTION node where
    // node->left->action() returns a boolean. The right part is the condition
    // body, that needs to be executed if the boolean is true.
    case COND:
        X(ret = process_rules(node->left, pkt, port, core));
        if (ret == 0) {
            return 0;
        }
        X(process_rules(node->right, pkt, port, core));
        return 1;

    // The left and the right part of a AND nodes are ACTION nodes where
    // node->{{side}}->action() returns a boolean. Return true if both
    // functions return true.
    case AND:
        X(ret = process_rules(node->left, pkt, port, core));
        if (!ret)
            return 0;
        X(ret = process_rules(node->right, pkt, port, core));
        return ret;

    // Almost like the AND node.
    case OR:
        X(ret = process_rules(node->left, pkt, port, core));
        if (ret)
            return 1;
        X(ret = process_rules(node->right, pkt, port, core));
        return ret;

    default:
        break ;
    }

    return 0;
}

#undef X

/*
 * Handle the ipv4 pkt:
 *  - if it is a ICMP message addressed to one of our interfaces, answer to it.
 *  - otherwise, process the configuration rules.
 */
int
ipv4_handle(struct rte_mbuf *pkt, uint8_t port, struct core *core)
{
    int ret;
    struct ipv4_hdr *ipv4_hdr;

    ipv4_hdr = ipv4_header(pkt);

    // TTL exceeded, don't answer and free the packet
    if (ipv4_hdr->time_to_live <= 1) {
        return -1;
    }
    ipv4_hdr->time_to_live--;

    if (ipv4_hdr->next_proto_id == IPPROTO_ICMP) {
        if ((ret = icmp_answer(pkt, port, core)) >= 0) {
            return ret;
        }
    }

    // No rules for this packet, free it
    if (core->app_config.rules == NULL) {
        return -1;
    }

    ret = process_rules(core->app_config.rules, pkt, port, core);
    return 0;
}
