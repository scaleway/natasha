#ifndef CONFIG_H_
#define CONFIG_H_

struct config_ctx {
    /* Current rule being parsed */
    int current_rule;

    /* Current action being parsed */
    int current_action;
};

#endif
