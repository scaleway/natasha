#include <math.h>
#include <rte_malloc.h>

#include "natasha.h"
#include "network_headers.h"
#include "actions.h"


// As defined in the ICMP RFC https://tools.ietf.org/html/rfc792 an error has
// the type 3.
static const int ICMP_TYPE_ERROR = 3;


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
    uint32_t res;

    if (lookup_table == NULL ||
        lookup_table[fstb] == NULL ||
        lookup_table[fstb][sndb] == NULL) {

        return -1;
    }

    res = lookup_table[fstb][sndb][l2b];
    if (res == 0) {
        return -1;
    }
    *value = res;
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
    uint32_t res;

    // If ip not found in lookup_table
    if (nat_lookup_ip(lookup_table, ip, &res) < 0) {
        rte_pktmbuf_free(pkt);
        return -1; // Stop processing next rules
    }

    *field = rte_cpu_to_be_32(res);
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
                        uint32_t *address, int inner_icmp_to_rewrite)
{
    struct ipv4_hdr *ipv4_hdr = ipv4_header(pkt);
    int ret;
    struct icmp_hdr *icmp_hdr;
    struct ipv4_hdr *inner_ipv4_hdr;
    uint32_t *inner_icmp_address;

    // Rewrite IPv4 source or destination address.
    ret = lookup_and_rewrite(pkt,
                             core->app_config->nat_lookup,
                             rte_be_to_cpu_32(*address),
                             address);
    // If the `address`is not in lookup table, it's an error and we should stop
    // processing rules for this packet (which has been freed by
    // lookup_and_rewrite()).
    if (ret < 0) {
        return ret;
    }

    // pkt is probably not an ICMP packet.
    if (likely(ipv4_hdr->next_proto_id != IPPROTO_ICMP)) {
        return ret;
    }

    icmp_hdr = icmp_header(pkt);

    // If ICMP, it is probably not an error.
    if (likely(icmp_hdr->icmp_type != ICMP_TYPE_ERROR)) {
        return ret;
    }

    // pkt is actually an ICMP error. Let's rewrite the inner IP packet.
    inner_ipv4_hdr = (struct ipv4_hdr *)(
        (unsigned char *)icmp_hdr + sizeof(*icmp_hdr)
    );

    if (inner_icmp_to_rewrite == IPV4_SRC_ADDR) {
        inner_icmp_address = &(inner_ipv4_hdr->src_addr);
    } else {
        inner_icmp_address = &(inner_ipv4_hdr->dst_addr);
    }

    // inner_ipv4_hdr contains the address to rewrite. Let's ensure this
    // address is really in the boundaries of `pkt`, as we wouldn't want to
    // dereference the inner IP header if it is outside of `pkt`. It can happen
    // if the ICMP error is forged, for instance with scapy.
    if (unlikely(
            ((uintptr_t)ipv4_hdr + rte_be_to_cpu_16(ipv4_hdr->total_length)) <
            (uintptr_t)inner_icmp_address + sizeof(*inner_icmp_address)
    )) {
        rte_pktmbuf_free(pkt);
        return -1;
    }

    return lookup_and_rewrite(
        pkt,
        core->app_config->nat_lookup,
        rte_be_to_cpu_32(*inner_icmp_address),
        inner_icmp_address
    );
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
 * Store NAT rules in nat_lookup.
 * @return
 *   - -1 on failure
 */
int
add_rules_to_table(uint32_t ****nat_lookup, uint32_t int_ip, uint32_t ext_ip,
                   unsigned int socket_id)
{
    *nat_lookup = add_rule_to_table(*nat_lookup, int_ip, ext_ip, socket_id);
    if (nat_lookup == NULL) {
        return -1;
    }

    *nat_lookup = add_rule_to_table(*nat_lookup, ext_ip, int_ip, socket_id);
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
