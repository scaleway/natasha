#include <rte_ip.h>

#include "natasha.h"
#include "network_headers.h"
#include "cond_network.h"


static struct ipv4_network int_pkt = {
    .ip=IPv4(10, 0, 0, 0),
    .mask=8
};

//static struct ipv4_network ext_pkt = {
//    .ip=IPv4(212, 47, 0, 0),
//    .mask=16
//};


/*
 * Load or reload configuration. If a configuration is already loaded,
 * free it.
 */
int
app_config_load(struct app_config *config, int argc, char **argv)
{
    config->ports[0].ip = IPv4(10, 2, 31, 11);
    config->ports[1].ip = IPv4(212, 47, 255, 91);

    config->rules[0].only_if.f = cond_ipv4_src_in_network;
    config->rules[0].only_if.params = &int_pkt;
    config->rules[0].actions[0].f = action_print;

    //config->rules[1].only_if.f = cond_ipv4_dst_in_network;
    //config->rules[1].only_if.params = &ext_pkt;
    //config->rules[1].actions[0].f = action_print;
    return 0;
}
