#include <rte_ip.h>

#include "natasha.h"
#include "network_headers.h"


int
app_config_parse(int argc, char **argv, struct app_config *config)
{
    memset(config, 0, sizeof(*config));

    config->ports[0].ip = IPv4(10, 2, 31, 11);
    config->ports[1].ip = IPv4(212, 47, 255, 91);

    config->rules[0].actions[0].f = action_print;
    return 0;
}
