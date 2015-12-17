include $(RTE_SDK)/mk/rte.vars.mk

APP = nat

CFLAGS += -O3 -g
CFLAGS += -Wall
CFLAGS += -I$(RTE_OUTPUT) # to include parseconfig.yy.h and parseconfig.tab.h
CFLAGS += -I$(RTE_SRCDIR) # to include files from parseconfig.yy.c and parseconfig.tab.c
# CFLAGS += $(WERROR_FLAGS)

SRCS-y :=                           \
    $(RTE_OUTPUT)/parseconfig.tab.c \
    $(RTE_OUTPUT)/parseconfig.yy.c  \
    action_out.c                    \
    action_log.c                    \
    action_nat.c                    \
    arp.c                           \
    cond_network.c                  \
    config.c                        \
    core.c                          \
    ipv4.c                          \
    pkt.c                           \


ifeq ($(NOT_FIRST_CALL),1)

natasha: $(RTE_OUTPUT)/parseconfig.yy.c $(RTE_OUTPUT)/parseconfig.yy.h $(RTE_OUTPUT)/parseconfig.tab.c $(RTE_OUTPUT)/parseconfig.tab.h all

$(RTE_OUTPUT)/parseconfig.yy.c $(RTE_OUTPUT)/parseconfig.yy.h: $(RTE_SRCDIR)/parseconfig.lex
	flex -o $(RTE_OUTPUT)/parseconfig.yy.c --header-file=$(RTE_OUTPUT)/parseconfig.yy.h $(RTE_SRCDIR)/parseconfig.lex

$(RTE_OUTPUT)/parseconfig.tab.c $(RTE_OUTPUT)/parseconfig.tab.h: $(RTE_SRCDIR)/parseconfig.y
	bison -d -o $(RTE_OUTPUT)/parseconfig.tab.c $(RTE_SRCDIR)/parseconfig.y

endif

include $(RTE_SDK)/mk/rte.extapp.mk
