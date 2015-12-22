%{
#include <stdio.h>

#include <rte_malloc.h>

#include "natasha.h"
#include "action_log.h"
#include "action_nat.h"
#include "action_out.h"
#include "cond_network.h"
#include "config.h"
%}

/* Make parser reentrant */
%define api.pure full
%lex-param   { yyscan_t scanner }
%parse-param { void *scanner }
/* Add params "config" and "ctx" to parsing functions */
%parse-param { struct app_config *config }
%parse-param { struct config_ctx *ctx }

%token OOPS

%token CONFIG_SECTION
%token RULES_SECTION

%token TOK_PORT
%token TOK_IP
%token TOK_NAT_RULE
%token TOK_NAT_REWRITE
%token TOK_IF
%token TOK_IN
%token TOK_OUT
%token TOK_MAC
%token TOK_PRINT

/* Possible data types for semantic values */
%union {
    int number;
    char *string;
    uint32_t ipv4_address;
    struct ether_addr mac;
    struct ipv4_network ipv4_network;
}

/* Semantic values */
%token <number> NUMBER
%token <ipv4_address> IPV4_ADDRESS
%token <ipv4_network> IPV4_NETWORK
%token <number> NAT_REWRITE_FIELD
%token <mac> MAC_ADDRESS

%{
#include "parseconfig.yy.h"

/* Declare yyerror prototype, of config.c */
void yyerror(yyscan_t scanner, struct app_config *config, struct config_ctx *ctx, const char *str);

#define CHECK_PTR(ptr) do {                                             \
    if ((ptr) == NULL) {                                                \
        yyerror(scanner, config, ctx, "Unable to allocate memory\n");   \
        YYERROR;                                                        \
    }                                                                   \
} while (0);

%}

%%

config_file:
    /* empty */
    | config_file config_section
    | config_file rules_section
;

/* config section */
config_section:
    CONFIG_SECTION '{' config_lines '}'
;

config_lines:
    /* empty */
    | config_lines config_port
    | config_lines config_nat_rule
;

config_port:
    TOK_PORT NUMBER[port] TOK_IP IPV4_ADDRESS[ip] ';'
    {
        if ($port >= RTE_MAX_ETHPORTS) {
            yyerror(scanner, config, ctx, "Invalid port number");
            YYERROR;
        }
        config->ports[$port].ip = $ip;
    }
;

config_nat_rule:
    TOK_NAT_RULE IPV4_ADDRESS[from] IPV4_ADDRESS[to] ';'
    {
        if (add_rules_to_table(&config->nat_lookup, $from, $to) < 0) {
            yyerror(scanner, config, ctx, "Unable to add NAT rule");
            YYERROR;
        }
    }
;

/* rules section */
rules_section:
    RULES_SECTION '{' rules_line '}'
;

rules_line:
    /* empty */
    | rules_line rules_cond    { ctx->current_action = 0; ctx->current_rule++; }
    | rules_line rules_action  { ctx->current_action = 0; ctx->current_rule++; }
;

rules_cond:
    TOK_IF NAT_REWRITE_FIELD[field] TOK_IN IPV4_NETWORK[network] '{' rules_actions '}'
    {
        struct ipv4_network *param;

        param = rte_malloc(NULL, sizeof(*param), 0);
        CHECK_PTR(param);
        *param = $network;

        if ($field == IPV4_SRC_ADDR) {
            config->rules[ctx->current_rule].only_if.f = cond_ipv4_src_in_network;
        } else {
            config->rules[ctx->current_rule].only_if.f = cond_ipv4_dst_in_network;
        }
        config->rules[ctx->current_rule].only_if.params = param;
    }
;

rules_actions:
    /* empty */
    | rules_actions rules_action
;

rules_action:
    rules_action_nat     { ctx->current_action++; }
    | rules_action_out   { ctx->current_action++; }
    | rules_action_print { ctx->current_action++; }
;

rules_action_nat:
    TOK_NAT_REWRITE NAT_REWRITE_FIELD[field] ';'
    {
        config->rules[ctx->current_rule].actions[ctx->current_action].f = action_nat_rewrite;

        if ($field == IPV4_SRC_ADDR) {
            config->rules[ctx->current_rule].actions[ctx->current_action].params = (void *)&IPV4_SRC_ADDR;
        } else {
            config->rules[ctx->current_rule].actions[ctx->current_action].params = (void *)&IPV4_DST_ADDR;
        }
    }
;

rules_action_out:
    TOK_OUT TOK_PORT NUMBER[port] TOK_MAC MAC_ADDRESS[mac] ';'
    {
        struct out_packet *out;

        out = rte_malloc(NULL, sizeof(*out), 0);
        CHECK_PTR(out);
        out->port = $port;
        out->vlan = -1;
        ether_addr_copy(&$mac, &out->next_hop);
        config->rules[ctx->current_rule].actions[ctx->current_action].f = action_out;
        config->rules[ctx->current_rule].actions[ctx->current_action].params = out;
    }
;

rules_action_print:
    TOK_PRINT ';'
    {
        config->rules[ctx->current_rule].actions[ctx->current_action].f = action_print;
    }
;
