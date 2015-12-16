#include <math.h>
#include <rte_malloc.h>

#include "natasha.h"
#include "network_headers.h"
#include "action_nat.h"

/*
 * Size of the first, second and third row of the NAT lookup table.
 */
static const int lkp_fs = 256; // 2^8
static const int lkp_ss = 256; // 2^8
static const int lkp_ts = 65536; // 2^16

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
static RULE_ACTION
lookup_and_rewrite(struct rte_mbuf *pkt, uint32_t ***lookup_table, uint32_t ip,
                   uint32_t *field)
{
    uint32_t res;

    if (nat_lookup_ip(lookup_table, ip, &res) < 0) {
        rte_pktmbuf_free(pkt);
        return ACTION_BREAK;
    }

    *field = rte_cpu_to_be_32(res);
    return ACTION_NEXT;
}

RULE_ACTION
action_nat_rewrite(struct rte_mbuf *pkt, uint8_t port, struct core *core, void *data)
{
    nat_rewrite_field_t field_to_rewrite = *(nat_rewrite_field_t *)data;
    struct ipv4_hdr *ipv4_hdr = ipv4_header(pkt);

    if (field_to_rewrite == IPV4_SRC_ADDR) {
        return lookup_and_rewrite(
            pkt, core->app_config.nat_lookup,
            rte_be_to_cpu_32(ipv4_hdr->src_addr),
            &ipv4_hdr->src_addr
        );
    } else {
        return lookup_and_rewrite(
            pkt, core->app_config.nat_lookup,
            rte_be_to_cpu_32(ipv4_hdr->dst_addr),
            &ipv4_hdr->dst_addr
        );
    }
}

/*
 * Set all the IP addresses stored in the NAT lookup table t to -1.
 */
static void
nat_reset_lookup_table(uint32_t ***t)
{
    int i, j;

    if (t == NULL) {
        return ;
    }

    for (i = 0; i < lkp_fs; ++i) {
        if (t[i] == NULL) {
            continue ;
        }

        for (j = 0; j < lkp_ss; ++j) {
            if (t[i][j] != NULL) {
                memset(t[i][j], 0, lkp_ts * sizeof(***t));
            }
        }
    }
}

static uint32_t ***
add_rule_to_table(uint32_t ***t, uint32_t key, uint32_t value)
{
    // first byte, second byte, last 2 bytes
    const int fstb = (key >> 24) & 0xff;
    const int sndb = (key >> 16) & 0xff;
    const int l2b = (key & 0xff00) | (key & 0xff);

    if (t == NULL) {
        t = rte_zmalloc(NULL, lkp_fs * sizeof(*t), 0);
        if (t == NULL) { return NULL; }
    }

    if (t[fstb] == NULL) {
        t[fstb] = rte_zmalloc(NULL, lkp_ss * sizeof(**t), 0);
        if (t[fstb] == NULL) { return NULL ; }
    }

    if (t[fstb][sndb] == NULL) {
        t[fstb][sndb] = rte_zmalloc(NULL, lkp_ts * sizeof(***t), 0);
        if (t[fstb][sndb] == NULL) { return NULL; }

        memset(t[fstb][sndb], 0, lkp_ts * sizeof(***t));
    }

    t[fstb][sndb][l2b] = value;
    return t;
}

/*
 * Feed the nat_lookup tree with the rules in filename.
 *
 * XXX:
 * - Remove the ugly use of getline
 * - Log parsing error instead of silently ignoring
 */
int
nat_reload(uint32_t ****nat_lookup, const char *filename)
{
    FILE *f;
    size_t n;
    char *line;

    nat_reset_lookup_table(*nat_lookup);
    if ((f = fopen(filename, "r")) == NULL) {
        return -1;
    }

    line = NULL;
    while (getline(&line, &n, f) != -1) {
        int ret;
        uint32_t s[4];
        uint32_t d[4];
        uint32_t src;
        uint32_t dst;

        ret = sscanf(line, "nat %3u.%3u.%3u.%3u -> %3u.%3u.%3u.%3u\n",
                     &s[0], &s[1], &s[2], &s[3],
                     &d[0], &d[1], &d[2], &d[3]);
        if (ret != 8) {
            continue ;
        }

        src = IPv4(s[0], s[1], s[2], s[3]);
        dst = IPv4(d[0], d[1], d[2], d[3]);

        *nat_lookup = add_rule_to_table(*nat_lookup, src, dst);
        if (nat_lookup == NULL) {
            goto err;
        }

        *nat_lookup = add_rule_to_table(*nat_lookup, dst, src);
        if (*nat_lookup == NULL) {
            goto err;
        }
    }

    free(line);
    fclose(f);
    return 0;

err:
    free(line);
    fclose(f);
    return -1;
}

/*
 * Display rules of the NAT lookup table.
 *
 * @return
 *  - Number of rules in nat_lookup.
 */
int
nat_dump_rules(uint32_t ***nat_lookup)
{
    size_t n;
    int i, j, k;

    printf("NAT RULES\n*********\n");

    if (nat_lookup == NULL)
        return 0;

    n = 0;
    for (i = 0; i < lkp_fs; ++i) {
        if (nat_lookup[i] == NULL) { continue ; }

        for (j = 0; j < lkp_ss; ++j) {
            if (nat_lookup[i][j] == NULL) { continue ; }

            for (k = 0; k < lkp_ts; ++k) {
                if (nat_lookup[i][j][k] == 0) { continue ; }

                ++n;

                printf(IPv4_FMT " -> " IPv4_FMT "\n",
                    IPv4_FMTARGS(IPv4(i, j, (k >> 8) & 0xff, k & 0xff)),
                    IPv4_FMTARGS(nat_lookup[i][j][k]));
            }
        }
    }
    return n;
}
