TEST = test_config
export SRCS-y = tests/$(TEST)/test.c

build_test: all
	$(Q)cp $(RTE_SRCDIR)/tests/$(TEST)/config_simple.conf $(RTE_OUTPUT)
	$(Q)echo [$(TEST)] built!

include $(RTE_SRCDIR)/Makefile
