/* vim: ts=4 sw=4 et */
include $(RTE_SDK)/mk/rte.vars.mk

# rte.extapp.mk creates the build directory, sets NOT_FIRST_CALL to 1 and
# recursively calls our Makefile.
ifeq ($(NOT_FIRST_CALL),1)

APP ?= nat

# Get the version
GIT_VERSION := $(shell git describe --dirty --always --tags)

CFLAGS += -DGIT_VERSION=\"$(GIT_VERSION)\"
CFLAGS += -O3 -g
CFLAGS += -flto
CFLAGS += -Wall
CFLAGS += -I$(RTE_OUTPUT) # to include bison and flex headers
CFLAGS += -I$(RTE_SRCDIR) # so bison and flex C files can include headers

ifdef UNITTEST
CFLAGS += -DUNITTEST
endif

# EXTRA_CFLAGS is defined in unittests
CFLAGS += $(EXTRA_CFLAGS)


FLEX_INPUT    = $(RTE_SRCDIR)/parseconfig.lex
FLEX_OUTPUT_H = $(RTE_OUTPUT)/parseconfig.yy.h
FLEX_OUTPUT_C = $(RTE_OUTPUT)/parseconfig.yy.c
FLEX_OUTPUT   = $(FLEX_OUTPUT_H) $(FLEX_OUTPUT_C)

BISON_INPUT    = $(RTE_SRCDIR)/parseconfig.y
BISON_OUTPUT_H = $(RTE_OUTPUT)/parseconfig.tab.h
BISON_OUTPUT_C = $(RTE_OUTPUT)/parseconfig.tab.c
BISON_OUTPUT   = $(BISON_OUTPUT_H) $(BISON_OUTPUT_C)

CONFIG_OUTPUT_H = $(FLEX_OUTPUT_H) $(BISON_OUTPUT_H)
CONFIG_OUTPUT_C = $(FLEX_OUTPUT_C) $(BISON_OUTPUT_C)
CONFIG_OUTPUT   = $(FLEX_OUTPUT) $(BISON_OUTPUT)

SRCS-y +=                           \
    $(CONFIG_OUTPUT_C)              \
	action_drop.c                   \
    action_out.c                    \
    action_log.c                    \
    action_nat.c                    \
	adm.c                           \
    arp.c                           \
    cond_network.c                  \
    cond_vlan.c                     \
    config.c                        \
    core.c                          \
    ipv4.c                          \
    pkt.c                           \

natasha: $(CONFIG_OUTPUT) all

$(FLEX_OUTPUT): $(FLEX_INPUT) natasha.h
	flex -o $(FLEX_OUTPUT_C) --header-file=$(FLEX_OUTPUT_H) $(FLEX_INPUT)

$(BISON_OUTPUT): $(BISON_INPUT) natasha.h
	bison -d -o $(BISON_OUTPUT_C) $(BISON_INPUT)

endif

include $(RTE_SDK)/mk/rte.extapp.mk
