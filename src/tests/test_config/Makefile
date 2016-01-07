TEST = test_config

export APP = bin
export SRCS-y = tests/$(TEST)/test.c

build_test: all
	$(Q)cp $(RTE_SRCDIR)/tests/$(TEST)/config_simple.conf $(RTE_OUTPUT)
	$(Q)cp $(RTE_SRCDIR)/tests/$(TEST)/config_simple.expect $(RTE_OUTPUT)

	$(Q)cp $(RTE_SRCDIR)/tests/$(TEST)/test.sh $(RTE_OUTPUT)/test
	$(Q)echo [$(TEST)] built!

include $(RTE_SRCDIR)/Makefile
