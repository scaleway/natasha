#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <rte_ip.h>

#include "natasha.h"
#include "network_headers.h"

#include "action_nat.h"
#include "action_out.h"
#include "cond_network.h"
#include "config.h"

#include "parseconfig.tab.h"
#include "parseconfig.yy.h"


void
yyerror(yyscan_t scanner, struct app_config *config, struct config_ctx *ctx,
        const char *str)
{
    RTE_LOG(EMERG, APP, "Parsing error on line %i: %s\n",
            yyget_lineno(scanner), str);
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
    struct config_ctx config_ctx;

    // Initialize an empty parsing context
    memset(&config_ctx, 0, sizeof(config_ctx));

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

    // Parse the configuration file
    yylex_init(&scanner);
    yyset_in(handle, scanner);
    ret = yyparse(scanner, config, &config_ctx);
    yylex_destroy(scanner);

    if (ret != 0) {
        goto err;
    }

    // Display NAT rules
    if (verbose) {
        nat_dump_rules(config->nat_lookup);
    }

    return 0;

err:
    return -1;
}
