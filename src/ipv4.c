#include <rte_ethdev.h>
#include <rte_prefetch.h>

#include <jit/jit.h>

#include "actions.h"
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

/*
 * Run the process_pkt JIT function.
 */
//static int
//process_pkt(struct rte_mbuf *pkt, uint8_t port, struct core *core)
//{
//    void *args[] = {
//        &pkt,
//        &port,
//        &core
//    };
//    int ret;
//
//    core->app_config->process_pkt(pkt, port, core);
//
//    //jit_function_apply(core->app_config->process_pkt, args, &ret);
//    // XXXXXXXXX: to improve performances *a lot* we need to call
//    // jit_function_to_closure instead of jit_function_apply
//    // http://eli.thegreenplace.net/2013/10/17/getting-started-with-libjit-part-1
//    return ret;
//}

//static int
//process_rules(struct app_config_node *node, struct rte_mbuf *pkt, uint8_t port,
//              struct core *core)
//{
//    jit_context_t context;
//
//	jit_type_t signature;
//	jit_type_t params[4];
//    static jit_function_t function;
//
//    jit_value_t args[4];
//
//    if (function == NULL) {
//    // Create a context to hold the JIT's primary state
//	context = jit_context_create();
//
//	// Lock the context while we build and compile the function
//	jit_context_build_start(context);
//
//    // Create the function signature that takes four params: pkt, port, core
//    // and data.
//	params[0] = jit_type_void_ptr;
//	params[1] = jit_type_uint;
//	params[2] = jit_type_void_ptr;
//	params[3] = jit_type_void_ptr;
//    signature = jit_type_create_signature(
//        jit_abi_cdecl, jit_type_uint, params, 4, 1
//    );
//
//    // Create the function object.
//	function = jit_function_create(context, signature);
//	//jit_type_free(signature);
//
//    args[0] = jit_value_get_param(function, 0);
//    args[1] = jit_value_get_param(function, 1);
//    args[2] = jit_value_get_param(function, 2);
//    args[3] = jit_value_get_param(function, 3);
//    jit_insn_call_native(
//        function, NULL, action_print,
//        signature,
//        args,
//        4,
//        JIT_CALL_NOTHROW);
//
//    // end
//    jit_function_compile(function);
//    jit_context_build_end(context);
//    }
//
//    // call function
//    int result = -42;
//    void *data = NULL;
//    void *x[] = {
//        &pkt,
//        &port,
//        &core,
//        &data
//    };
//
//    jit_function_apply(function, x, &result);
//    printf("result: %i\n", result);
//
//    // destroy
//	//jit_context_destroy(context);
//
//    action_drop(pkt, port, core, NULL);
//    return 0;
//}

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

    // XXX: the old AST interpreter used to return -1. Is it still the case for
    // this version?
    // process_pkt returns -1 if it encounters a breaking rule (eg.
    // action_out or action_drop). We don't want to return -1 because the
    // caller function – dispatch_packet() in core.c – would free pkt.
    core->app_config->process_pkt(pkt, port, core);

    return 0;
}
