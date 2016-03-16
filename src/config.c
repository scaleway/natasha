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

    // Free ports IP addresses
    for (i = 0; i < sizeof(config->ports) / sizeof(*config->ports); ++i) {
        struct app_config_port_ip_addr *ip;
        struct app_config_port_ip_addr *next;

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
}

/*
 * Load configuration into `config`.
 */
int
app_config_load(struct app_config *config, int argc, char **argv,
                unsigned int socket_id)
{
    int i;
    char *config_file;
    FILE *handle;
    yyscan_t scanner;
    int ret;

    memset(config, 0, sizeof(*config));

    config_file = "/etc/natasha.conf";

    // Parse argv. Can't use getopt, since option parsing needs to be
    // reentrant.
    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-f") == 0) {
            if (i == argc - 1) {
                RTE_LOG(EMERG, APP, "Filename required for -f\n");
                return -1;
            }
            config_file = argv[i + 1];
            ++i;
            continue ;
        } else {
            RTE_LOG(EMERG, APP, "Unknown option: %s\n", argv[i]);
            return -1;
        }
    }

    handle = fopen(config_file, "r");
    if (handle == NULL) {
        RTE_LOG(EMERG, APP, "Fail to load %s: %s\n",
                config_file, strerror(errno));
        return -1;
    }

    // Parse configuration file
    yylex_init(&scanner);
    yyset_in(handle, scanner);
    ret = yyparse(scanner, config, socket_id);

    // Free handle and files opened during parsing
    free_flex_buffers(scanner);

    yylex_destroy(scanner);

    if (ret != 0) {
        return -1;
    }

    return 0;
}

/*
 * Reload the configuration of each worker.
 */
int
app_config_reload_all(struct core *cores, int argc, char **argv, int out_fd)
{
    unsigned int core;
    struct app_config master_config;

    // Ensure configuration is valid
    if (app_config_load(&master_config, argc, argv, SOCKET_ID_ANY) < 0) {
        dprintf(out_fd, "Unable to load configuration. This is "
                        "probably due to a syntax error, but you should check "
                        "server logs. Workers have not been reloaded.\n");
        return -1;
    }

    // Reload workers
    RTE_LCORE_FOREACH_SLAVE(core) {
        unsigned int socket_id;
        struct app_config old_config;
        struct app_config new_config;

        socket_id = rte_lcore_to_socket_id(core);

        if (app_config_load(&new_config, cores[core].app_argc,
                            cores[core].app_argv, socket_id) < 0) {
            dprintf(
                out_fd,
                "Core %i: unable to load configuration. Check server logs. "
                "Following workers are not reloaded.",
                core
            );
            app_config_free(&master_config);
            return -1;
        }

        // Switch to the new configuration
        rte_rwlock_write_lock(&cores[core].app_config_lock);
        old_config = cores[core].app_config;
        cores[core].app_config = new_config;
        rte_rwlock_write_unlock(&cores[core].app_config_lock);

        app_config_free(&old_config);
    }

    dprintf(out_fd, "%i NAT rules reloaded\n",
            nat_number_of_rules(master_config.nat_lookup));
    app_config_free(&master_config);
    return 0;
}
