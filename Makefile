ifeq ($(RTE_SDK),)
$(error "Please define RTE_SDK environment variable")
endif

ifndef RTE_TARGET
export RTE_TARGET=x86_64-native-linuxapp-gcc
endif

export RTE_SRCDIR=$(abspath src)
export RTE_OUTPUT=$(abspath build)


all:
	$(MAKE) -C . -f $(RTE_SRCDIR)/Makefile


# Dynamically discover unittests
TESTS=$(shell find $(RTE_SRCDIR)/tests                  \
		-mindepth 1 -maxdepth 1 -type d         \
		-exec basename {} \;)

test:
	# Building tests...
	@for test in $(TESTS); do                                   \
		$(MAKE) -C . -f $(RTE_SRCDIR)/tests/$$test/Makefile \
			--no-print-directory                        \
			APP=$$test                                  \
			RTE_OUTPUT=$(RTE_OUTPUT)/$$test             \
			UNITTEST=1                                  \
			build_test                                  \
	; done
	# Running tests...
	@for test in $(TESTS); do                                   \
		$(RTE_OUTPUT)/$$test/$$test                         \
		&& echo [OK] $$test || echo [FAIL] $$test           \
	; done
