ifeq ($(RTE_SDK),)
$(error "Please define RTE_SDK environment variable")
endif

# Default target, can be overriden by command line or environment
RTE_TARGET ?= x86_64-native-linuxapp-gcc

RTE_SRCDIR=$(abspath src)
RTE_OUTPUT=$(abspath build)

include src/Makefile
