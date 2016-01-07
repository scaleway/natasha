#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <rte_ip.h>
#include <rte_malloc.h>

#include "natasha.h"
#include "network_headers.h"

#include "action_nat.h"
#include "action_out.h"
#include "cond_network.h"

#include "parseconfig.tab.h"
#include "parseconfig.yy.h"


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

/*
 * Load or reload configuration. If a configuration is already loaded,
 * free it.
 */
int
app_config_reload(struct app_config *config, int argc, char **argv,
                  int verbose)
{
    int i;
    char *config_file;
    FILE *handle;
    yyscan_t scanner;
    int ret;

    handle = NULL;

    config_file = "/etc/natasha.conf";

    // Parse argv. Can't use getopt, since option parsing needs to be
    // reentrant.
    for (i = 1; i < argc; ++i) {

        if (strcmp(argv[i], "-f") == 0) {
            if (i == argc - 1) {
                RTE_LOG(EMERG, APP, "Filename required for -f\n");
                goto err;
            }
            config_file = argv[i + 1];
            ++i;
            continue ;
        } else {
            RTE_LOG(EMERG, APP, "Unknown option: %s\n", argv[i]);
            goto err;
        }
    }

    handle = fopen(config_file, "r");
    if (handle == NULL) {
        RTE_LOG(EMERG, APP, "Fail to load %s: %s\n",
                config_file, strerror(errno));
        goto err;
    }

    // Remove NAT rules from lookup table. New rules are added during
    // configuration parsing.
    nat_reset_lookup_table(config->nat_lookup);

    // Free old configuration
    config->rules = reset_rules(config->rules);

    // Parse the configuration file
    yylex_init(&scanner);
    yyset_in(handle, scanner);
    ret = yyparse(scanner, config);
    yylex_destroy(scanner);

    if (ret != 0) {
        goto err;
    }

    // Display NAT rules
    if (verbose) {
        nat_dump_rules(NULL, config->nat_lookup);
    }

    fclose(handle);
    return 0;

err:
    if (handle) {
        fclose(handle);
    }
    return -1;
}
