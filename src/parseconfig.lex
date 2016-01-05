/*
 * See docs/CONFIGURATION.md
 */

/* Generate a default yywrap function. */
%option noyywrap
/* Do not generate warnings because yyunput/input are unused. */
%option nounput noinput
/* Make the lexer reentrant. */
%option reentrant bison-bridge
/* Track line numbers. */
%option   yylineno
/* Do not create a default rule to echo unmatched tokens. */
%option nodefault

%{
#include <stdio.h>

#include <rte_ip.h>

#include "natasha.h"
#include "action_nat.h"
#include "cond_network.h"
#include "config.h"
#include "parseconfig.tab.h"
%}

/*
 * set of chars that should be returned as single-character tokens.
*/
self [;{}\(\)]

/* 0 to 255 */
BYTE            [01]?[0-9]?[0-9]|2[0-4][0-9]|25[0-5]
IPV4_ADDRESS    ({BYTE}\.){3}{BYTE}
IPV4_NETWORK    {IPV4_ADDRESS}\/([0-9]|[1-2][0-9]|3[0-2])
MAC_ADDRESS     ([0-9a-z]{2}:){5}[0-9a-z]{2}
%%

[ \t\n]*    /* ignore spaces */;
#.*         /* ignore comments */;

{self}  return yytext[0];

"config" return CONFIG_SECTION;
"rules"  return RULES_SECTION;

[0-9]+              yylval->number = atoi(yytext); return NUMBER;

{IPV4_ADDRESS} {
    int buf[4];

    if (sscanf(yytext, "%3u.%3u.%3u.%3u", &buf[0], &buf[1], &buf[2], &buf[3]) != 4) {
        return OOPS;
    }
    yylval->ipv4_address = IPv4(buf[0], buf[1], buf[2], buf[3]);
    return IPV4_ADDRESS;
}

{IPV4_NETWORK} {
    int buf[5];

    if (sscanf(yytext, "%3u.%3u.%3u.%3u/%2u", &buf[0], &buf[1], &buf[2],
               &buf[3], &buf[4]) != 5) {
        return OOPS;
    }
    yylval->ipv4_network.ip = IPv4(buf[0], buf[1], buf[2], buf[3]);
    yylval->ipv4_network.mask = buf[4];
    return IPV4_NETWORK;
}

{MAC_ADDRESS} {
    int buf[6];

    if (sscanf(yytext, "%2x:%2x:%2x:%2x:%2x:%2x", &buf[0], &buf[1], &buf[2],
               &buf[3], &buf[4], &buf[5]) != 6) {
        return OOPS;
    }
    yylval->mac.addr_bytes[0] = buf[0];
    yylval->mac.addr_bytes[1] = buf[1];
    yylval->mac.addr_bytes[2] = buf[2];
    yylval->mac.addr_bytes[3] = buf[3];
    yylval->mac.addr_bytes[4] = buf[4];
    yylval->mac.addr_bytes[5] = buf[5];
    return MAC_ADDRESS;
}

"port"         return TOK_PORT;
"ip"           return TOK_IP;
"nat rule"     return TOK_NAT_RULE;
"nat rewrite"  return TOK_NAT_REWRITE;
"if"           return TOK_IF;
"else"         return TOK_ELSE;
"and"          return TOK_AND;
"or"           return TOK_OR;
"in"           return TOK_IN;
"out"          return TOK_OUT;
"mac"          return TOK_MAC;
"print"        return TOK_PRINT;

ipv4\.src_addr  yylval->number = IPV4_SRC_ADDR; return NAT_REWRITE_FIELD;
ipv4\.dst_addr  yylval->number = IPV4_DST_ADDR; return NAT_REWRITE_FIELD;

<*>.|\n     return OOPS /* default rule */;
