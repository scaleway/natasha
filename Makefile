ifeq ($(RTE_SDK),)
$(error "Please define RTE_SDK environment variable")
endif

# Default target, can be overriden by command line or environment
RTE_TARGET ?= x86_64-native-linuxapp-gcc

include $(RTE_SDK)/mk/rte.vars.mk

APP = nat

SRCS-y := 		\
	arp.c		\
	config.c 	\
	core.c 		\
	ipv4.c		\
	pkt.c 		\

CFLAGS += -O3 -g
CFLAGS += -Wall
# CFLAGS += $(WERROR_FLAGS)

include $(RTE_SDK)/mk/rte.extapp.mk
