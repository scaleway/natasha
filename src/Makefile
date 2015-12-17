include $(RTE_SDK)/mk/rte.vars.mk

APP = nat

CFLAGS += -O3 -g
CFLAGS += -Wall
# CFLAGS += $(WERROR_FLAGS)

SRCS-y :=           \
    action_out.c    \
    action_log.c    \
    action_nat.c    \
    arp.c           \
    cond_network.c  \
    config.c        \
    core.c          \
    ipv4.c          \
    pkt.c           \

include $(RTE_SDK)/mk/rte.extapp.mk
