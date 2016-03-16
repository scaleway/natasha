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

/* Declare context include_ctx */
%x include_ctx

%{
#include <stdio.h>

#include <rte_ip.h>

#include "actions.h"
#include "conds.h"
#include "natasha.h"

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

<*>[ \t\n]*    /* ignore spaces */;
<*>#.*         /* ignore comments */;

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
"mtu"          return TOK_MTU;
"ip"           return TOK_IP;
"vlan"         return TOK_VLAN;
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
"drop"         return TOK_DROP;

ipv4\.src_addr  yylval->number = IPV4_SRC_ADDR; return NAT_REWRITE_FIELD;
ipv4\.dst_addr  yylval->number = IPV4_DST_ADDR; return NAT_REWRITE_FIELD;


"!include"[ \t] {
    // Start the include context
    BEGIN(include_ctx);
}

<include_ctx>[^ \t\n]+  {
    FILE *newfile;
    YY_BUFFER_STATE state;

    // We're in the include context. Open the file to include.
    if ((newfile = fopen(yytext, "r")) == NULL) {
        fprintf(stderr, "Unable to !include %s: %s\n",
                yytext, strerror(errno));
        return OOPS;
    }

    // * Create a new buffer for the included file
    // * Push it on top of the buffers stack and make it active
    //   (yyin = newfile after yypush_buffer_state()).
    state = yy_create_buffer(newfile, YY_BUF_SIZE, yyscanner);
    yypush_buffer_state(state, yyscanner);

    // Start the default context with the new active buffer.
    BEGIN(INITIAL);
}

<<EOF>> {
    // Parsing successfully finished for the current file, close it.
    fclose(yyin);

    // Remove the current buffer from the top of the buffers stack and continue
    // with the previous one.
    yypop_buffer_state(yyscanner);

    // There's no previous buffer, stop.
    if (!YY_CURRENT_BUFFER) {
        yyterminate();
    }
}

<*>.|\n     return OOPS /* default rule */;

%%

/*
 * This function must be called after yyparse() to free resources.
 *
 * In case of parsing error, the special rule <<EOF>> is not reached and the
 * files aren't closed.
 * If the parsing was successful, YY_CURRENT_BUFFER is false and nothing is
 * done.
 */
void free_flex_buffers(yyscan_t scanner) {
    struct yyguts_t *yyg = (struct yyguts_t*)scanner;

    while (YY_CURRENT_BUFFER) {
        fclose(yyin);
        yypop_buffer_state(scanner);
    }
}
