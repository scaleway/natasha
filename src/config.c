#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <rte_ip.h>
#include <rte_malloc.h>

#include <jit/jit.h>

#include "natasha.h"
#include "network_headers.h"

#include "actions.h"
#include "conds.h"

#include "parseconfig.tab.h"
#include "parseconfig.yy.h"


// Defined in parseconfig.lex
void free_flex_buffers(yyscan_t scanner);


/*
 * Returns the JIT signature of actions_* and cond_* functions, ie. a function
 * that takes four params (pkt, port, core, data) and returns an int.
 *
 * The caller needs to free jit_type_free(signature).
 */
static jit_type_t
get_natasha_sig()
{
    jit_type_t signature;
    jit_type_t params[] = {
        jit_type_void_ptr,
        jit_type_uint,
        jit_type_void_ptr,
        jit_type_void_ptr
    };

    signature = jit_type_create_signature(
        jit_abi_cdecl,                    // abi
        jit_type_int,                     // return type
        params,                           // params
        sizeof(params) / sizeof(*params), // num params
        1                                 // incref
    );

    return signature;
}

/*
 * Call a cond_* or a action_* function exported by natasha.
 */
jit_value_t
call_natasha(jit_function_t jit_function,
             int (*func)(struct rte_mbuf *, uint8_t, struct core *, void *),
             void *data,
             size_t datasize)
{
    jit_type_t signature;
    jit_value_t ret;
    jit_value_t jit_args[4];
    jit_value_t jit_data;

    // Get the JIT signature of func, which takes four arguments.
    signature = get_natasha_sig();

    if (!data) {
        // Create NULL pointer.
        jit_data = jit_value_create_nint_constant(jit_function,
                                                  jit_type_void_ptr, 0);
    } else {
        jit_value_t jit_datasize;
        size_t i;

        // Create a JIT constant to hold datasize.
        jit_datasize = jit_value_create_nint_constant(jit_function,
                                                      jit_type_uint, datasize);
        // Allocate enough memory on the stack to hold data.
        jit_data = jit_insn_alloca(jit_function, jit_datasize);
        // Copy data in jit_data byte per byte.
        for (i = 0; i < datasize; ++i) {
            jit_insn_store_relative(
                jit_function,
                jit_data,
                i,
                jit_value_create_nint_constant(jit_function, jit_type_ubyte,
                                               ((char *)data)[i])
            );
        }
    }

    jit_args[0] = jit_value_get_param(jit_function, 0);
    jit_args[1] = jit_value_get_param(jit_function, 1);
    jit_args[2] = jit_value_get_param(jit_function, 2);
    jit_args[3] = jit_data;

    // Call func.
    ret = jit_insn_call_native(
        jit_function,                         // jit function
        NULL,                                 // name
        func,                                 // native func ptr
        signature,                            // native func signature
        jit_args,                             // arguments for jit function
        sizeof(jit_args) / sizeof(*jit_args), // arguments size
        JIT_CALL_NOTHROW                      // do not optimize
    );

    jit_type_free(signature);

    // ret = call ret
    // if (ret < 0) { return ret; }
    // -1 = terminal action
    jit_label_t after_return = jit_label_undefined;

    jit_value_t zero = jit_value_create_nint_constant(jit_function, jit_type_uint, 0);
    jit_value_t is_terminal_rule = jit_insn_lt(jit_function, ret, zero);

    jit_insn_branch_if_not(jit_function, is_terminal_rule, &after_return);

    jit_insn_return(jit_function, ret);
    jit_insn_label(jit_function, &after_return);

    return ret;
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
    //config->rules = reset_rules(config->rules);

    rte_free(config);
}

static int
parse_config(yyscan_t scanner, struct app_config *config,
             unsigned int socket_id)
{
    // XXX: check return values
    jit_context_t context;
    jit_type_t signature;
    jit_function_t process_pkt;

    jit_type_t params[] = {
        jit_type_void_ptr, // struct rte_mbuf *pkt
        jit_type_uint, // uint8_t port
        jit_type_void_ptr // struct core *core
    };

    // Create a context to hold the JIT's primary state.
    context = jit_context_create();

    // Lock the context while we build and compile the function.
    jit_context_build_start(context);

    // Signature of the JIT function to process packets.
    signature = jit_type_create_signature(
        jit_abi_cdecl,                    // abi
        jit_type_int,                     // return type
        params,                           // params
        sizeof(params) / sizeof(*params), // num params
        1                                 // incref
    );
    jit_value_t drop_ret;

    process_pkt = jit_function_create(context, signature);
    jit_type_free(signature);

    // Use flex/bison to parse natasha.conf.
    //
    // When yyparse returns:
    // - `config` is updated to contain the application configuration (ports IP
    //   addresses, ...) as specified in the "config" section of natasha.conf.
    // - `process_pkt` is updated to contain the libjit'ed function of what to
    //    do for each packet, as specified by the "rules" section of natasha.conf.
    if (yyparse(scanner, config, socket_id, context, process_pkt)) {
        RTE_LOG(EMERG, APP, "Unable to parse configuration file\n");
        return -1;
    }

    // If we reach this JIT part, it means yyparse() didn't update process_pkt,
    // probably because the rules{} section of natasha.conf is empty. Let's
    // drop the packet.
    drop_ret = call_natasha(process_pkt, &action_drop, NULL, 0);
    jit_insn_return(process_pkt, drop_ret);

    // Compile "func" before converting it to a closure. If we don't compile it
    // explicitly, the compilation will be done when the closure is called for
    // the first time which causes an overhead when dealing with the first
    // packet.
    if (!jit_function_compile(process_pkt)) {
        RTE_LOG(EMERG, APP, "Unable to compile JIT function\n");
        return -1;
    }

    // To call a libjit jit_function_t created with jit_function_create(), you
    // can either call jit_function_apply() which does some checks to ensure
    // the ABI is correct (which is expensive), or convert the jit_function_t to
    // a closure with jit_function_to_closure() which returns a pointer to
    // function that you can call directly, without overhead.
    config->process_pkt = jit_function_to_closure(process_pkt);

    jit_context_build_end(context);

    return 0;
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
                goto error;
            }
            config_file = argv[i + 1];
            ++i;
            continue ;
        } else {
            RTE_LOG(EMERG, APP, "Unknown option: %s\n", argv[i]);
            goto error;
        }
    }

    handle = fopen(config_file, "r");
    if (handle == NULL) {
        RTE_LOG(EMERG, APP, "Fail to load %s: %s\n",
                config_file, strerror(errno));
        goto error;
    }

    // Parse configuration file
    if (yylex_init(&scanner)) {
        fclose(handle);
        goto error;
    }

    yyset_in(handle, scanner);

    ret = parse_config(scanner, config, socket_id);

    // Free handle and files opened during parsing
    free_flex_buffers(scanner);

    yylex_destroy(scanner);

    if (ret != 0) {
        goto error;
    }

    return config;

error:
    rte_free(config);
    return NULL;
}

/*
 * Whether or not the PMD supports per-queue statistics.
 * Currently, ixgbe supports them but not i40e (see DPDK source code at
 * drivers/net/i40e/i40e_ethdev.c:i40e_dev_queue_stats_mapping).
 */
int
support_per_queue_statistics(uint8_t port)
{
    struct rte_eth_dev_info dev_info;

    rte_eth_dev_info_get(port, &dev_info);
    return !!strcmp(dev_info.driver_name, "net_i40e");
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
            while (new_config->used == 0) {
                continue ;
            }
            app_config_free(old_config);
        }
    }

    dprintf(out_fd, "%i NAT rules reloaded\n",
            nat_number_of_rules(master_config->nat_lookup));
    app_config_free(master_config);
    return 0;
}
