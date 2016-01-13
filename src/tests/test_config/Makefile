TEST = test_config

export APP = test_config_bin
export SRCS-y = tests/$(TEST)/main.c

build_test: all
	$(Q)cp $(RTE_SRCDIR)/tests/$(TEST)/*.conf $(RTE_OUTPUT)
	$(Q)cp $(RTE_SRCDIR)/tests/$(TEST)/*.expect $(RTE_OUTPUT)

	$(Q)cp $(RTE_SRCDIR)/tests/$(TEST)/test.sh $(RTE_OUTPUT)/test
	$(Q)echo [$(TEST)] built!

include $(RTE_SRCDIR)/Makefile
