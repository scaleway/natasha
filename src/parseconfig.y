%{
/*
 * See docs/CONFIGURATION.md
 */

#include <stdio.h>

#include <rte_malloc.h>

#include "actions.h"
#include "conds.h"
#include "natasha.h"
%}

/* Make parser reentrant */
%define api.pure full
%lex-param   { yyscan_t scanner }
%parse-param { void *scanner }
/* Add param "config" and "socket_id" to parsing functions */
%parse-param { struct app_config *config }
%parse-param { unsigned int socket_id }

%token OOPS

%token CONFIG_SECTION
%token RULES_SECTION

%token TOK_PORT
%token TOK_IP
%token TOK_MTU
%token TOK_VLAN
%token TOK_NAT_RULE
%token TOK_NAT_REWRITE
%token TOK_IF
%token TOK_ELSE
%token TOK_AND
%token TOK_OR
%token TOK_IN
%token TOK_OUT
%token TOK_MAC
%token TOK_PRINT
%token TOK_DROP

/*
 * Explicit to bison that AND and OR are left-associative, otherwise a
 * shift/reduce warning is raised.
 */
%left TOK_AND TOK_OR

/* Possible data types for semantic values */
%union {
    int number;
    char *string;
    uint32_t ipv4_address;
    struct ether_addr mac;
    struct ipv4_network ipv4_network;
    struct app_config_node *config_node;
    struct app_config_port_ip_addr *port_ip_addrs;
}

/* Semantic values */
%token <number>         NUMBER
%token <ipv4_address>   IPV4_ADDRESS
%token <ipv4_network>   IPV4_NETWORK
%token <number>         NAT_REWRITE_FIELD
%token <mac>            MAC_ADDRESS

/* Config section */
%type<number>          config_port_opt_mtu
%type<number>          config_port_opt_vlan
%type<port_ip_addrs>   config_port_extra_ips

/* Rules types */
%type<config_node> rules_content
%type<config_node> rules_stmt
%type<config_node> opt_else

%type<config_node> cond
%type<config_node> cond_in_network
%type<config_node> cond_vlan

%type<config_node> action
%type<config_node> action_nat_rewrite
%type<config_node> action_out
%type<number>      action_out_opt_vlan
%type<config_node> action_print
%type<config_node> action_drop

%{
#include "parseconfig.yy.h"

static void
yyerror(yyscan_t scanner, struct app_config *config, unsigned int socket_id, const char *str)
{
    RTE_LOG(EMERG, APP, "Parsing error on line %i: %s\n",
            yyget_lineno(scanner), str);
}

#define CHECK_PTR(ptr) do {                                                    \
    if ((ptr) == NULL) {                                                       \
        yyerror(scanner, config, socket_id, "Unable to allocate memory\n");    \
        YYERROR;                                                               \
    }                                                                          \
} while (0)

%}

%%

config_file:
    /* empty */
    | config_file config_section
    | config_file rules_section
;

/*
 * CONFIG SECTION
 */
config_section:
    CONFIG_SECTION '{' config_lines '}'
;

config_lines:
    /* empty */
    | config_lines ';'
    | config_lines config_port
    | config_lines config_nat_rule
;

/* port n [mtu MTU] [vlan VLAN] ip IP [[vlan VLAN] ip IP...] */
config_port:
    TOK_PORT NUMBER[port]
    config_port_opt_mtu[mtu]
    config_port_opt_vlan[vlan] TOK_IP IPV4_ADDRESS[ip]
    config_port_extra_ips[next_ips] ';' {
        struct app_config_port_ip_addr *port_ip;

        if ($port >= RTE_MAX_ETHPORTS) {
            yyerror(scanner, config, socket_id, "Invalid port number");
            YYERROR;
        }

        port_ip = rte_zmalloc_socket(NULL, sizeof(*port_ip), 0, socket_id);
        CHECK_PTR(port_ip);

        port_ip->addr.ip = $ip;
        port_ip->addr.vlan = $vlan;
        port_ip->next = $next_ips;

        config->ports[$port].mtu = $mtu;
        config->ports[$port].ip_addresses = port_ip;
    }
;

config_port_opt_mtu:
    /* empty */             { $$ = 1500; }
    | TOK_MTU NUMBER[mtu]   { $$ = $mtu; }
;

config_port_opt_vlan:
    /* empty */             { $$ = 0; }
    | TOK_VLAN NUMBER[vlan] { $$ = $vlan; }
;

config_port_extra_ips:
    /* empty */ {
        $$ = NULL;
    }
    | config_port_opt_vlan[vlan] TOK_IP IPV4_ADDRESS[ip] config_port_extra_ips[next] {
        struct app_config_port_ip_addr *port_ip;

        port_ip = rte_zmalloc_socket(NULL, sizeof(*port_ip), 0, socket_id);
        CHECK_PTR(port_ip);
        port_ip->addr.ip = $ip;
        port_ip->addr.vlan = $vlan;
        port_ip->next = $next;
        $$ = port_ip;
    }
;


config_nat_rule:
    TOK_NAT_RULE IPV4_ADDRESS[from] IPV4_ADDRESS[to] ';'
    {
        if (add_rules_to_table(&config->nat_lookup, $from, $to, socket_id) < 0) {
            yyerror(scanner, config, socket_id, "Unable to add NAT rule");
            YYERROR;
        }
    }
;


/*
 * RULES SECTION
 */
rules_section:
    RULES_SECTION '{' rules_content[root] '}' {
        config->rules = $root;
    }
;

rules_content:
    /* empty */ { $$ = NULL; }
    | rules_content[prev] rules_stmt[new] {

        // Do not create a sequence node if this is the first action
        if ($prev == NULL) {
            $$ = $new;
        }
        // Do not create a sequence node if $new is an empty statement
        else if ($new == NULL) {
            $$ = $prev;
        }
        else {
            struct app_config_node *node;

            node = rte_zmalloc_socket(NULL, sizeof(*node), 0, socket_id);
            CHECK_PTR(node);

            node->type = SEQ;
            node->left = $prev;
            node->right = $new;

            $$ = node;
        }
    }
;

rules_stmt:
    ';' { $$ = NULL; }
    | TOK_IF '(' cond[what] ')' '{' rules_content[body] '}' opt_else[else] {
        struct app_config_node *if_node;
        struct app_config_node *cond_node;

        if_node = rte_zmalloc_socket(NULL, sizeof(*if_node), 0, socket_id);
        CHECK_PTR(if_node);

        cond_node = rte_zmalloc_socket(NULL, sizeof(*cond_node), 0, socket_id);
        CHECK_PTR(cond_node);

        if_node->type = IF;
        if_node->left = cond_node;
        if_node->right = $else;

        cond_node->type = COND;
        cond_node->left = $what;
        cond_node->right = $body;

        $$ = if_node;
    }
    | action
;

opt_else:
    /* empty */                            { $$ = NULL; }
    | TOK_ELSE '{' rules_content[body] '}' { $$ = $body; }
;

cond:
    cond[lhs] TOK_AND cond[rhs] {
        struct app_config_node *node;

        node = rte_zmalloc_socket(NULL, sizeof(*node), 0, socket_id);
        CHECK_PTR(node);

        node->type = AND;
        node->left = $lhs;
        node->right = $rhs;

        $$ = node;
    }
    | cond[lhs] TOK_OR cond[rhs] {
        struct app_config_node *node;

        node = rte_zmalloc_socket(NULL, sizeof(*node), 0, socket_id);
        CHECK_PTR(node);

        node->type = OR;
        node->left = $lhs;
        node->right = $rhs;

        $$ = node;
    }
    | cond_in_network
    | cond_vlan
;

cond_in_network:
    NAT_REWRITE_FIELD[field] TOK_IN IPV4_NETWORK[network] {
        struct app_config_node *node;
        struct ipv4_network *data;

        node = rte_zmalloc_socket(NULL, sizeof(*node), 0, socket_id);
        CHECK_PTR(node);

        data = rte_zmalloc_socket(NULL, sizeof(*data), 0, socket_id);
        CHECK_PTR(data);

        *data = $network;

        node->type = ACTION;

        if ($field == IPV4_SRC_ADDR) {
            node->action = cond_ipv4_src_in_network;
        } else {
            node->action = cond_ipv4_dst_in_network;
        }
        node->data = data;

        $$ = node;
    }
;

cond_vlan:
    TOK_VLAN NUMBER[vlan] {
        struct app_config_node *node;
        int *data;

        node = rte_zmalloc_socket(NULL, sizeof(*node), 0, socket_id);
        CHECK_PTR(node);

        data = rte_zmalloc_socket(NULL, sizeof(*data), 0, socket_id);
        CHECK_PTR(data);

        *data = $vlan;

        node->type = ACTION;
        node->action = cond_vlan;
        node->data = data;

        $$ = node;
    }
;

action:
    action_nat_rewrite
    | action_out
    | action_print
    | action_drop
;

action_nat_rewrite:
    TOK_NAT_REWRITE NAT_REWRITE_FIELD[field] ';' {
        struct app_config_node *node;
        int *data;

        node = rte_zmalloc_socket(NULL, sizeof(*node), 0, socket_id);
        CHECK_PTR(node);

        data = rte_zmalloc_socket(NULL, sizeof(*data), 0, socket_id);
        CHECK_PTR(data);

        if ($field == IPV4_SRC_ADDR) {
            *data = IPV4_SRC_ADDR;
        } else {
            *data = IPV4_DST_ADDR;
        }

        node->type = ACTION;
        node->action = action_nat_rewrite;
        node->data = data;

        $$ = node;
    }
;

action_out:
    TOK_OUT TOK_PORT NUMBER[port] TOK_MAC MAC_ADDRESS[mac] action_out_opt_vlan[vlan] ';' {
        struct app_config_node *node;
        struct out_packet *data;

        node = rte_zmalloc_socket(NULL, sizeof(*node), 0, socket_id);
        CHECK_PTR(node);

        data = rte_zmalloc_socket(NULL, sizeof(*data), 0, socket_id);
        CHECK_PTR(data);

        data->port = $port;
        data->next_hop = $mac;
        data->vlan = $vlan;

        node->type = ACTION;
        node->action = action_out;
        node->data = data;

        $$ = node;
    }
;

action_out_opt_vlan:
    /* empty */             { $$ = 0; }
    | TOK_VLAN NUMBER[vlan] { $$ = $vlan; }

action_print:
    TOK_PRINT {
        struct app_config_node *node;

        node = rte_zmalloc_socket(NULL, sizeof(*node), 0, socket_id);
        CHECK_PTR(node);

        node->type = ACTION;
        node->action = action_print;

        $$ = node;
    }
;

action_drop:
    TOK_DROP {
        struct app_config_node *node;

        node = rte_zmalloc_socket(NULL, sizeof(*node), 0, socket_id);
        CHECK_PTR(node);

        node->type = ACTION;
        node->action = action_drop;

        $$ = node;
    }
;
