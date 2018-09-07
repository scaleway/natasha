#include <math.h>

#include <rte_malloc.h>

#include "natasha.h"
#include "network_headers.h"
#include "actions.h"

/*
 * Search for ip in the NAT lookup table, and store the result in value.
 *
 * @return
 *  - -1 if ip is not in lookup_table.
 */
static int
nat_lookup_ip(uint32_t ***lookup_table, uint32_t ip, uint32_t *value)
{
    // first byte, second byte, last 2 bytes
    const int fstb = (ip >> 24) & 0xff;
    const int sndb = (ip >> 16) & 0xff;
    const int l2b = (ip & 0xff00) | (ip & 0xff);

    if (lookup_table == NULL ||
        lookup_table[fstb] == NULL ||
        lookup_table[fstb][sndb] == NULL ||
        lookup_table[fstb][sndb][l2b] == 0)
        return -1;

    *value = lookup_table[fstb][sndb][l2b];

    return 0;
}

/*
 * Search `ip` in `lookup_table` and rewrite `field` with the value. If `ip` is
 * not found, drop `pkt`.
 */
static int
lookup_and_rewrite(struct rte_mbuf *pkt, uint32_t ***lookup_table, uint32_t ip,
                   uint32_t *field)
{
    // If ip not found in lookup_table
    if (nat_lookup_ip(lookup_table, ip, field) < 0) {
        rte_pktmbuf_free(pkt);
        return -1; // Stop processing next rules
    }

    return 0;
}

static int
icmp_nat_handle(struct core *core, struct rte_mbuf *pkt,
                int inner_ipv4_to_rewrite)
{

    struct ipv4_hdr *ipv4_hdr = ipv4_header(pkt);
    struct icmp_hdr *icmp_hdr = icmp_header(pkt);
    struct ipv4_hdr *inner_ipv4_hdr;
    uint32_t *inner_ipv4_address;
    uint32_t old_ipv4_address;
    uint32_t old_ipv4_cksum;

    /* no need to handle other type payload that is not affected by nat */
    if (likely(icmp_hdr->icmp_type != ICMP_DEST_UNREACH &&
               icmp_hdr->icmp_type != ICMP_TIME_EXCEEDED &&
               icmp_hdr->icmp_type != ICMP_PARAMETERPROB))
        return 0;

    /* If pkt is actually an ICMP error, let's rewrite the inner IP packet. If */
    /* the outer packet is not large enough to contain a full IPv4 header, then */
    /* we don't rewrite anything. */
    /* We probalby should drop the packet instead, as it has probably been */
    /* forged (with Scapy for instance). I don't think there are legitimate */
    /* cases where an ICMP error doesn't contain the IPv4 header of the packet */
    /* originating the error, but just in case I prefer not to drop anything. */
    if (unlikely(rte_be_to_cpu_16(ipv4_hdr->total_length)
                    - sizeof(struct ipv4_hdr) /* outer IPv4 header */
                    - sizeof(struct icmp_hdr) /* ICMP error header */
                    - sizeof(struct ipv4_hdr) /* inner IPv4 header */
                    ) < 0)
        return -1;

    inner_ipv4_hdr = (struct ipv4_hdr *)((unsigned char *)icmp_hdr +
                                         sizeof(*icmp_hdr));

    if (inner_ipv4_to_rewrite == IPV4_SRC_ADDR) {
        inner_ipv4_address = &(inner_ipv4_hdr->src_addr);
        old_ipv4_cksum = inner_ipv4_hdr->hdr_checksum;
    } else {
        inner_ipv4_address = &(inner_ipv4_hdr->dst_addr);
        old_ipv4_cksum = inner_ipv4_hdr->hdr_checksum;
    }

    old_ipv4_address = *inner_ipv4_address;
    if (lookup_and_rewrite(pkt,
                           core->app_config->nat_lookup,
                           rte_be_to_cpu_32(*inner_ipv4_address),
                           inner_ipv4_address) < 0) {
        core->stats->drop_no_rule++;
        return -1;
    }

    /* Since we udpated the inner IPv4 packet, its checksum needs to be updated */
    /* using the incremental update. */
    cksum_update(&inner_ipv4_hdr->hdr_checksum, old_ipv4_address,
                 *inner_ipv4_address);

    /* Update ICMP checksum when updating: */
    /* 1) Inner ipv4 checksum */
    cksum_update(&icmp_hdr->icmp_cksum, old_ipv4_cksum,
                 inner_ipv4_hdr->hdr_checksum);
    /* 2) Inner ipv4 address */
    cksum_update(&icmp_hdr->icmp_cksum, old_ipv4_address, *inner_ipv4_address);

    return 0;
}
/*
 * The actual rewrite function, to avoid duplicating the code twice to handle
 * the rewriting of the source and the destination addresses.
 *
 * We also handle the special case of ICMP error messages:
 *   https://tools.ietf.org/html/rfc5508
 *
 * The most famous ICMP packets have the type "echo" or "reply", which are
 * notably used by "ping".
 *
 * The type "error" also exists. It can be used to return an error like
 * "Destination network unreachable", "Destination port unreachable", or any of
 * the other errors:
 *   https://en.wikipedia.org/wiki/Internet_Control_Message_Protocol
 *
 * An ICMP error message is always a response to a packet. For example, if you
 * try to reach a destination with a too small TTL, an intermediate hop will
 * return an ICMP error "Time to live exceeded".
 *
 * This error message contains data, which contain the IP header of the packet
 * which originated the error.
 *
 * In the previous example, the "Time to live exceeded" packet is formed as
 * follow:
 *
 * OUTER IP HEADER | IP DATA (the ICMP packet) | ICMP DATA (the inner IP
 *                                                          header)
 *
 * The outer IP packet's source address is the intermediate hop's address, the
 * destination is the client's address.
 * The inner IP packet's source address is the client's address, the
 * destination is the destination it was trying to reach.
 *
 * When rewriting the **source** address, the inner packet's **destination**
 * address needs to be updated.
 * When rewriting the **destination** address, the inner packet's **source**
 * address needs to be updated.
 */
static int
action_nat_rewrite_impl(struct rte_mbuf *pkt, uint8_t port, struct core *core,
                        uint32_t *address, int inner_ipv4_to_rewrite)
{
    struct ipv4_hdr *ipv4_hdr = ipv4_header(pkt);
    uint32_t save_ipv4 = *address;

    // Rewrite IPv4 source or destination address.
    if (lookup_and_rewrite(pkt,
                           core->app_config->nat_lookup,
                           rte_be_to_cpu_32(*address),
                           address) < 0) {
    // If the `address`is not in lookup table, it's an error and we should stop
    // processing rules for this packet (which has been freed by
    // lookup_and_rewrite()).
        core->stats->drop_no_rule++;
        return -1;
    }

    /* Update IP checksum using incremental update */
    cksum_update(&ipv4_hdr->hdr_checksum, save_ipv4, *address);

    /* Update L4 checksums on all packet a part from [2nd, n] fragment */
    /* offload the checksum when possible */
    switch (NATA_IS_FRAG(ipv4_hdr) ? 0 : ipv4_hdr->next_proto_id) {
    case IPPROTO_TCP:
    {
        struct tcp_hdr *tcp_hdr = tcp_header(pkt);

        if (unlikely(NATA_IS_FIRST_FRAG(ipv4_hdr))) {
            tcp_hdr->cksum -= save_ipv4 & 0xffff;
            tcp_hdr->cksum -= save_ipv4>>16 & 0xffff;
            tcp_hdr->cksum += *address & 0xffff;
            tcp_hdr->cksum += *address>>16 & 0xffff;
        } else {
            tcp_hdr->cksum = 0;
            pkt->ol_flags |= PKT_TX_TCP_CKSUM;
        }
        break;
    }
    case IPPROTO_UDPLITE:
    case IPPROTO_UDP:
    {
        struct udp_hdr *udp_hdr = udp_header(pkt);

        if (unlikely(NATA_IS_FIRST_FRAG(ipv4_hdr))) {
            udp_hdr->dgram_cksum -= save_ipv4 & 0xffff;
            udp_hdr->dgram_cksum -= save_ipv4>>16 & 0xffff;
            udp_hdr->dgram_cksum += *address & 0xffff;
            udp_hdr->dgram_cksum += *address>>16 & 0xffff;
        } else {
            udp_hdr->dgram_cksum = 0;
            pkt->ol_flags |= PKT_TX_UDP_CKSUM;
        }
        break;
    }
    case IPPROTO_ICMP:
    {
        /* Handle inner Ipv4 header in ICMP error message */
        return icmp_nat_handle(core, pkt, inner_ipv4_to_rewrite);
    }
    default:
        break;
    }

    return 0;
}

/*
 * See documentation of action_nat_rewrite_impl.
 */
int
action_nat_rewrite(struct rte_mbuf *pkt, uint8_t port, struct core *core, void *data)
{
    int field_to_rewrite = *(int *)data;
    struct ipv4_hdr *ipv4_hdr = ipv4_header(pkt);

    if (field_to_rewrite == IPV4_SRC_ADDR) {
        return action_nat_rewrite_impl(
            pkt,
            port,
            core,
            &ipv4_hdr->src_addr,
            IPV4_DST_ADDR
        );
    }
    return action_nat_rewrite_impl(
        pkt,
        port,
        core,
        &ipv4_hdr->dst_addr,
        IPV4_SRC_ADDR
    );
}

/*
 * Set all the IP addresses stored in the NAT lookup table t to -1.
 */
void
nat_reset_lookup_table(uint32_t ***t)
{
    int i, j;

    if (t) {
        for (i = 0; i < lkp_fs; ++i) {
            if (t[i]) {
                for (j = 0; j < lkp_ss; ++j) {
                    rte_free(t[i][j]);
                }
                rte_free(t[i]);
            }
        }
        rte_free(t);
    }
}

static uint32_t ***
add_rule_to_table(uint32_t ***t, uint32_t key, uint32_t value,
                  unsigned int socket_id)
{
    // first byte, second byte, last 2 bytes
    const int fstb = (key >> 24) & 0xff;
    const int sndb = (key >> 16) & 0xff;
    const int l2b = (key & 0xff00) | (key & 0xff);

    if (t == NULL) {
        t = rte_zmalloc_socket(NULL, lkp_fs * sizeof(*t), 0, socket_id);
        if (t == NULL) {
            return NULL;
        }
    }

    if (t[fstb] == NULL) {
        t[fstb] = rte_zmalloc_socket(NULL, lkp_ss * sizeof(**t), 0, socket_id);
        if (t[fstb] == NULL) {
            rte_free(t);
            return NULL ;
        }
    }

    if (t[fstb][sndb] == NULL) {
        t[fstb][sndb] = rte_zmalloc_socket(NULL, lkp_ts * sizeof(***t), 0,
                                           socket_id);
        if (t[fstb][sndb] == NULL) {
            rte_free(t[fstb]);
            rte_free(t);
            return NULL;
        }

        memset(t[fstb][sndb], 0, lkp_ts * sizeof(***t));
    }

    t[fstb][sndb][l2b] = value;
    return t;
}

/*
 * Store NAT rules in nat_lookup in network order.
 * @return
 *   - -1 on failure
 */
int
add_rules_to_table(uint32_t ****nat_lookup, uint32_t int_ip, uint32_t ext_ip,
                   unsigned int socket_id)
{
    uint32_t ext_ip_formated = rte_cpu_to_be_32(ext_ip);
    uint32_t int_ip_formated = rte_cpu_to_be_32(int_ip);

    *nat_lookup = add_rule_to_table(*nat_lookup, int_ip,
                                    ext_ip_formated, socket_id);
    if (nat_lookup == NULL) {
        return -1;
    }

    *nat_lookup = add_rule_to_table(*nat_lookup, ext_ip,
                                    int_ip_formated, socket_id);
    if (nat_lookup == NULL) {
        return -1;
    }

    return 0;
}

static int
nat_iter(uint32_t ***nat_lookup,
         void (*func)(uint32_t from, uint32_t to, void *arg),
         void *data)
{
    int i, j, k;
    uint32_t from, to;
    size_t n;

    if (nat_lookup == NULL)
        return 0;

    n = 0;
    for (i = 0; i < lkp_fs; ++i) {
        if (nat_lookup[i] == NULL) {
            continue ;
        }

        for (j = 0; j < lkp_ss; ++j) {
            if (nat_lookup[i][j] == NULL) {
                continue ;
            }

            for (k = 0; k < lkp_ts; ++k) {
                if (nat_lookup[i][j][k] == 0) {
                    continue ;
                }

                if (func) {
                    from = IPv4(i, j, (k >> 8) & 0xff, k & 0xff);
                    to = nat_lookup[i][j][k];
                    func(from, to, data);
                }
                ++n;
            }
        }
    }
    // Since each rule is stored twice, one time for each direction, there are
    // actually n / 2 NAT rules in nat_lookup.
    return n / 2;
}

static void
nat_dump_rule(uint32_t from, uint32_t to, void *arg)
{
    int out_fd = *(int *)arg;

    dprintf(out_fd, IPv4_FMT " -> " IPv4_FMT "\n",
            IPv4_FMTARGS(from), IPv4_FMTARGS(to));
}

/*
 * Display rules of the NAT lookup table.
 *
 * @return
 *  - Number of rules in nat_lookup.
 */
int
nat_dump_rules(int out_fd, uint32_t ***nat_lookup)
{
    size_t size;

    size = nat_iter(nat_lookup, &nat_dump_rule, &out_fd);
    return size;
}

int
nat_number_of_rules(uint32_t ***nat_lookup)
{
    return nat_iter(nat_lookup, NULL, NULL);
}
