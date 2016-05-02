%{
/*
 * See docs/CONFIGURATION.md
 */

#include <stdio.h>

#include <rte_malloc.h>

#include <jit/jit.h>

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
%parse-param { jit_context_t context }
%parse-param { jit_function_t process_pkt }

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
    struct app_config_port_ip_addr *port_ip_addrs;
    jit_label_t *jit_plabel;
    jit_value_t jit_value;
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
%type<jit_value> cond
%type<jit_value> cond_in_network
%type<jit_value> cond_vlan

%type<number> action_out_opt_vlan


%{
#include "parseconfig.yy.h"

static void
yyerror(yyscan_t scanner,
        struct app_config *config,
        unsigned int socket_id,
        jit_context_t context,
        jit_function_t process_pkt,
        const char *str)
{
    RTE_LOG(EMERG, APP, "Parsing error on line %i: %s\n",
            yyget_lineno(scanner), str);
}

#define CHECK_PTR(ptr) do {                                  \
    if ((ptr) == NULL) {                                     \
        yyerror(scanner, config, socket_id, context,         \
                process_pkt, "Unable to allocate memory\n"); \
        YYERROR;                                             \
    }                                                        \
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
            yyerror(scanner, config, socket_id, context, process_pkt,
                    "Invalid port number");
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
            yyerror(scanner, config, socket_id, context, process_pkt,
                    "Unable to add NAT rule");
            YYERROR;
        }
    }
;

/*
 * RULES SECTION
 */
rules_section:
    RULES_SECTION '{' rules_content[root] '}'
;

rules_content:
    /* empty */
    | rules_content[prev] rules_stmt[new]
;

rules_stmt:
    ';'
    | TOK_IF              // $1
      '(' cond[what] ')'  // $2, $3, $4
      '{'                 // $5
      {
        jit_label_t *else_clause;

        else_clause = rte_zmalloc_socket(NULL, sizeof(*else_clause), 0, socket_id);
        CHECK_PTR(else_clause);
        *else_clause = jit_label_undefined;

        // If $what is false, jump to else clause. Otherwise continue.
        jit_insn_branch_if_not(process_pkt, $what, else_clause);

        $<jit_plabel>$ = else_clause; // make else_clause available later by using the rule number, ie. $<type>6
      }
      {
        jit_label_t *end_clause;

        end_clause = rte_zmalloc_socket(NULL, sizeof(*end_clause), 0, socket_id);
        CHECK_PTR(end_clause);
        *end_clause = jit_label_undefined;

        $<jit_plabel>$ = end_clause; // make end_clause available later by using the rule number, ie. $<type>7
       }
       rules_content[body]
       '}'
       {
         // End of "if" clause. Jump to the end and insert the "else" label.
         jit_label_t *else_label = $<jit_plabel>6;
         jit_label_t *end_clause = $<jit_plabel>7;

         jit_insn_branch(process_pkt, end_clause);
         jit_insn_label(process_pkt, else_label);
       }
       opt_else[else]
       {
         // End of "else" clause. Insert "end" label.
         jit_label_t *end_clause = $<jit_plabel>7;

         jit_insn_label(process_pkt, end_clause);
         // XXX: free labels
       }
    | action
;

opt_else:
    /* empty */
    | TOK_ELSE '{' rules_content[body] '}'
;

cond:
    cond[lhs] TOK_AND cond[rhs] {
        $$ = jit_insn_and(process_pkt, $lhs, $rhs);
    }
    | cond[lhs] TOK_OR cond[rhs] {
        $$ = jit_insn_or(process_pkt, $lhs, $rhs);
    }
    | cond_in_network
    | cond_vlan
;

cond_in_network:
    NAT_REWRITE_FIELD[field] TOK_IN IPV4_NETWORK[network] {
        if ($field == IPV4_SRC_ADDR) {
            $$ = call_natasha(process_pkt, &cond_ipv4_src_in_network, &$network, sizeof($network));
        } else {
            $$ = call_natasha(process_pkt, &cond_ipv4_dst_in_network, &$network, sizeof($network));
        }
    }
;

cond_vlan:
    TOK_VLAN NUMBER[vlan] {
        // XXX do we really need a malloc here? how can we free it?
        int *data;

        data = rte_zmalloc_socket(NULL, sizeof(*data), 0, socket_id);
        CHECK_PTR(data);

        *data = $vlan;

        $$ = call_natasha(process_pkt, &cond_vlan, data, sizeof(data));
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
        call_natasha(process_pkt, &action_nat_rewrite, &$field, sizeof($field));
    }
;

action_out:
    TOK_OUT TOK_PORT NUMBER[port] TOK_MAC MAC_ADDRESS[mac] action_out_opt_vlan[vlan] ';' {
        struct out_packet data = {
            .port     = $port,
            .vlan     = $vlan,
            .next_hop = $mac
        };
        call_natasha(process_pkt, &action_out, &data, sizeof(data));
    }
;

action_out_opt_vlan:
    /* empty */             { $$ = 0; }
    | TOK_VLAN NUMBER[vlan] { $$ = $vlan; }

action_print:
    TOK_PRINT {
        call_natasha(process_pkt, &action_print, NULL, 0);
    }
;

action_drop:
    TOK_DROP {
        call_natasha(process_pkt, &action_drop, NULL, 0);
    }
;
