#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <rte_ip.h>
#include <rte_malloc.h>

#include "natasha.h"
#include "network_headers.h"

#include "actions.h"
#include "conds.h"

#include "parseconfig.tab.h"
#include "parseconfig.yy.h"


// Defined in parseconfig.lex
void free_flex_buffers(yyscan_t scanner);


static struct app_config_node *
reset_rules(struct app_config_node *root)
{
    if (!root) {
        return NULL;
    }

    if (root->left) {
        reset_rules(root->left);
    }

    if (root->right) {
        reset_rules(root->right);
    }

    rte_free(root->data);
    rte_free(root);

    return NULL;
}

void
app_config_free(struct app_config *config)
{
    uint8_t i;

    if (config == NULL) {
        return ;
    }

    // Free ports IP addresses
    for (i = 0; i < sizeof(config->ports) / sizeof(*config->ports); ++i) {
        struct port_ip_addr *ip;
        struct port_ip_addr *next;

        ip = config->ports[i].ip_addresses;
        while (ip) {
            next = ip->next;
            rte_free(ip);
            ip = next;
        }
        config->ports[i].ip_addresses = NULL;
    }

    // Empty NAT lookup table
    nat_reset_lookup_table(config->nat_lookup);

    // Free packet rules
    config->rules = reset_rules(config->rules);

    rte_free(config);
}

/*
 * Load and return configuration.
 */
struct app_config *
app_config_load(int argc, char **argv, unsigned int socket_id)
{
    int i;
    struct app_config *config;
    char *config_file;
    FILE *handle;
    yyscan_t scanner;
    int ret;

    config = rte_zmalloc(NULL, sizeof(*config), 0);
    if (config == NULL) {
        RTE_LOG(EMERG, APP, "app_config_load zmalloc failed\n");
        return NULL;
    }

    config_file = "/etc/natasha.conf";

    // Parse argv. Can't use getopt, since option parsing needs to be
    // reentrant.
    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-f") == 0) {
            if (i == argc - 1) {
                RTE_LOG(EMERG, APP, "Filename required for -f\n");
                rte_free(config);
                return NULL;
            }
            config_file = argv[i + 1];
            ++i;
            continue ;
        } else {
            RTE_LOG(EMERG, APP, "Unknown option: %s\n", argv[i]);
            rte_free(config);
            return NULL;
        }
    }

    handle = fopen(config_file, "r");
    if (handle == NULL) {
        RTE_LOG(EMERG, APP, "Fail to load %s: %s\n",
                config_file, strerror(errno));
        rte_free(config);
        return NULL;
    }

    // Parse configuration file
    if (yylex_init(&scanner)) {
        fclose(handle);
        rte_free(config);
        return NULL;
    }

    yyset_in(handle, scanner);
    ret = yyparse(scanner, config, socket_id);

    // Free handle and files opened during parsing
    free_flex_buffers(scanner);

    yylex_destroy(scanner);

    if (ret != 0) {
        app_config_free(config);
        return NULL;
    }

    return config;
}

/*
 * Whether or not the PMD supports per-queue statistics.
 * Currently, ixgbe supports them but not i40e nor mlx5 (see DPDK documentation)
 * this is a DIRTY workaround should be fixed asap.
 */
int
support_per_queue_statistics(uint8_t port)
{
    struct rte_eth_dev_info dev_info;

    rte_eth_dev_info_get(port, &dev_info);
    return !!strcmp(dev_info.driver_name, "net_i40e") &&
	    !!strcmp(dev_info.driver_name, "net_mlx5");
}

/*
 * Reload the configuration of each worker.
 */
int
app_config_reload_all(struct core *cores, int argc, char **argv, int out_fd)
{
    unsigned int core;
    struct app_config *master_config;

    // Ensure configuration is valid
    master_config = app_config_load(argc, argv, SOCKET_ID_ANY);
    if (master_config == NULL) {
        dprintf(out_fd, "Unable to load configuration. This is "
                        "probably due to a syntax error, but you should check "
                        "server logs. Workers have not been reloaded.\n");
        return -1;
    }

    // Reload workers
    RTE_LCORE_FOREACH_SLAVE(core) {
        unsigned int socket_id;
        struct app_config *old_config;
        struct app_config *new_config;

        socket_id = rte_lcore_to_socket_id(core);

        if ((new_config = app_config_load(argc, argv, socket_id)) == NULL) {
            dprintf(
                out_fd,
                "Core %i: unable to load configuration. Check server logs. "
                "Following workers are not reloaded.",
                core
            );
            app_config_free(master_config);
            return -1;
        }

        // Switch to the new configuration
        old_config = cores[core].app_config;
        cores[core].app_config = new_config;

        // If there's an old config (ie. we're in the context of a reload, and
        // not at application startup), wait until the new configuration is
        // used and free the old config.
        if (old_config) {
            while (!(new_config->flags & NAT_FLAG_USED))
                continue;
            app_config_free(old_config);
        }
    }

    dprintf(out_fd, "%i NAT rules reloaded\n",
            nat_number_of_rules(master_config->nat_lookup));
    app_config_free(master_config);
    return 0;
}
