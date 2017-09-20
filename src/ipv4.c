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

    // Compute IPv4 checksum
    ipv4_hdr->hdr_checksum = 0;
    ipv4_hdr->hdr_checksum = rte_ipv4_cksum(ipv4_hdr);

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
    struct ipv4_hdr *ipv4_hdr;
    uint32_t dst_ip;

    ipv4_hdr = ipv4_header(pkt);
    dst_ip = rte_be_to_cpu_32(ipv4_hdr->dst_addr);

    if (!is_natasha_ip(core->app_config, dst_ip, VLAN_ID(pkt))) {
        return -1;
    }

    // Even if we can't handle pkt, it is addressed to us. Drop it and
    // return 0 to mark it as processed.
    if (icmp_dispatch(pkt, port, core) < 0) {
        rte_pktmbuf_free(pkt);
    }
    return 0;
}

#define X(cond) do {    \
    if ((cond) < 0) {   \
        return -1;      \
    }                   \
} while (0)

/*
 * Process the rules AST for pkt.
 *
 * See detailed documentation in docs/CONFIGURATION.md.
 */
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
 * Fix CISCO Nexus 9000 series bug when untagging a packet.
 *
 * Bug explaination:
 *
 * An Ethernet frame containing an IPv4 packet is formed as follow:
 *
 *      MAC_DEST MAC_SOURCE [VLAN] LENGTH PAYLOAD CRC
 *
 * where:
 *
 * - VLAN is optional
 * - PAYLOAD is at minimum 46 bytes long if VLAN is absent, otherwise 42. In
 *   this example, it contains the IPv4 packet.
 * - CRC is the checksum computed from every field except VLAN.
 *
 * When a Nexus 9000 removes the VLAN from an Ethernet frame, it should adjust
 * the payload and add NULL bytes in the case the payload size is below 46.
 * Instead, it adds random bytes which causes the CRC to be considered invalid.
 *
 * This function updates the PAYLOAD padding and force it to only contain NULL
 * bytes, so the frame becomes:
 *
 *      MAC_DEST MAC_SOURCE LENGTH PAYLOAD PADDING CRC
 *
 * where length of PAYLOAD+PADDING is at least 46, and PADDING only contains
 * zeros.
 *
 * Consequently:
 * - this function has no effect if there's no padding in pkt (ie. payload is
 *   bigger than 46).
 * - if pkt doesn't come from a buggy Nexus 9000, this function has no effect
 *   since it rewrites the padding to 0, which was already set to 0.
 */
static inline void
fix_nexus9000_padding_bug(struct rte_mbuf *pkt)
{
    struct ipv4_hdr *ipv4_hdr;
    uint16_t ipv4_len;
    unsigned char *pkt_data;
    int padding_len;

    ipv4_hdr = ipv4_header(pkt);
    ipv4_len = rte_be_to_cpu_16(ipv4_hdr->total_length);

    // pkt->pkt_len is at least bigger than a IPv4 packet, otherwise we
    // wouldn't be here since pkt would not have been recognized as a
    // ETHER_TYPE_IPv4 packet in core.c
    //
    // ipv4_len is read from the ipv4 header, which is a 16 bits integer user
    // input.
    //
    // We don't want padding_len to be inferior to 0, as it would cause a
    // buffer overflow when casted as unsigned in memset.
    padding_len = pkt->pkt_len - sizeof(struct ether_hdr) - ipv4_len;

    if (padding_len > 0) {
        pkt_data = rte_pktmbuf_mtod(pkt, unsigned char *);
        memset(pkt_data + sizeof(struct ether_hdr) + ipv4_len,
               0x00,
               padding_len);
    }
}

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

    fix_nexus9000_padding_bug(pkt);

    ipv4_hdr = ipv4_header(pkt);

    // TTL exceeded, don't answer and free the packet
    if (unlikely(ipv4_hdr->time_to_live <= 1)) {
        return -1;
    }
    ipv4_hdr->time_to_live--;

    if (unlikely(ipv4_hdr->next_proto_id == IPPROTO_ICMP)) {
        if ((ret = icmp_answer(pkt, port, core)) >= 0) {
            return ret;
        }
    }

    // No rules for this packet, free it
    if (unlikely(core->app_config->rules == NULL)) {
        return -1;
    }

    // process_rules returns -1 if it encounters a breaking rule (eg.
    // action_out or action_drop). We don't want to return -1 because the
    // caller function – dispatch_patcher() in core.c – would free pkt.
    (void)process_rules(core->app_config->rules, pkt, port, core);

    return 0;
}
