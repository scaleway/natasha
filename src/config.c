#include <rte_ip.h>

#include "natasha.h"
#include "network_headers.h"

#include "action_nat.h"
#include "action_out.h"

#include "cond_network.h"


static struct ipv4_network int_pkt = {
    .ip=IPv4(10, 0, 0, 0),
    .mask=8
};

static struct ipv4_network ext_pkt = {
    .ip=IPv4(212, 47, 0, 0),
    .mask=16
};

static struct out_packet out_port_0 = {
    .port=0,
    .vlan=-1,
};

static struct out_packet out_port_1 = {
    .port=1,
    .vlan=-1,
};


nat_rewrite_field_t REWRITE_SRC = IPV4_SRC_ADDR;
nat_rewrite_field_t REWRITE_DST = IPV4_DST_ADDR;

/*
 * To prevent each core from disploying the new configuration, use a global
 * flag. There is no locking mechanism on purpose. A race condition could
 * occur, leading to the configuration being displayed by several cores.
 */
static int verbose = 0;

/*
 * Load or reload configuration. If a configuration is already loaded,
 * free it.
 */
int
app_config_reload(struct app_config *config, int argc, char **argv)
{
    int v = 0;

    if (++verbose == 1) {
        v = 1;
    }

    config->ports[0].ip = IPv4(10, 2, 31, 11);
    config->ports[1].ip = IPv4(212, 47, 255, 91);

    if (nat_reload(&config->nat_lookup, "/tmp/nat_rules.conf") < 0) {
        goto err;
        return -1;
    }

    // 7c:0e:ce:25:f3:97
    out_port_0.next_hop.addr_bytes[0] = 0x7c;
    out_port_0.next_hop.addr_bytes[1] = 0x0e;
    out_port_0.next_hop.addr_bytes[2] = 0xce;
    out_port_0.next_hop.addr_bytes[3] = 0x25;
    out_port_0.next_hop.addr_bytes[4] = 0xf3;
    out_port_0.next_hop.addr_bytes[5] = 0x97;
    out_port_1.next_hop = out_port_0.next_hop;

    if (v) {
        nat_dump_rules(config->nat_lookup);
    }

    config->rules[0].only_if.f = cond_ipv4_src_in_network;
    config->rules[0].only_if.params = &int_pkt;
    config->rules[0].actions[0].f = action_nat_rewrite;
    config->rules[0].actions[0].params = &REWRITE_SRC;
    config->rules[0].actions[1].f = action_out;
    config->rules[0].actions[1].params = &out_port_1;

    config->rules[1].only_if.f = cond_ipv4_dst_in_network;
    config->rules[1].only_if.params = &ext_pkt;
    config->rules[1].actions[0].f = action_nat_rewrite;
    config->rules[1].actions[0].params = &REWRITE_DST;
    config->rules[1].actions[1].f = action_out;
    config->rules[1].actions[1].params = &out_port_0;

    --verbose;
    return 0;

err:
    --verbose;
    return -1;
}
