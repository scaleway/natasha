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
 * Search ip in lookup_table and rewrite field. If ip is not found, drop pkt.
 */
static int
lookup_and_rewrite(struct rte_mbuf *pkt, uint32_t ***lookup_table, uint32_t ip,
                   uint32_t *field)
{
    uint32_t res;

    if (nat_lookup_ip(lookup_table, ip, &res) < 0) {
        rte_pktmbuf_free(pkt);
        return -1; // stop processing next rules
    }

    *field = rte_cpu_to_be_32(res);
    return 0;
}

int
action_nat_rewrite(struct rte_mbuf *pkt, uint8_t port, struct core *core, void *data)
{
    int field_to_rewrite = *(int *)data;
    struct ipv4_hdr *ipv4_hdr = ipv4_header(pkt);

    if (field_to_rewrite == IPV4_SRC_ADDR) {
        return lookup_and_rewrite(
            pkt, core->app_config->nat_lookup,
            rte_be_to_cpu_32(ipv4_hdr->src_addr),
            &ipv4_hdr->src_addr
        );
    } else {
        return lookup_and_rewrite(
            pkt, core->app_config->nat_lookup,
            rte_be_to_cpu_32(ipv4_hdr->dst_addr),
            &ipv4_hdr->dst_addr
        );
    }
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
