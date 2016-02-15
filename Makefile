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
TESTS=$(shell find $(RTE_SRCDIR)/tests    \
         -mindepth 1 -maxdepth 1 -type d  \
         -exec basename {} \;)

test:
	# Building tests...
	@for test in $(TESTS); do                               \
		$(MAKE) -C . -f $(RTE_SRCDIR)/tests/$$test/Makefile \
			--no-print-directory                            \
			RTE_OUTPUT=$(RTE_OUTPUT)/$$test                 \
			UNITTEST=1                                      \
			build_test                                      \
	; done
	# Running tests...
	@for test in $(TESTS); do                                \
		sudo $(RTE_OUTPUT)/$$test/test                       \
                  && echo "[OK] $$test" ||                   \
                           echo "[FAIL] $$test (status=$$?)" \
	; done


# Dynamically discover reports
REPORTS=$(shell find $(RTE_SRCDIR)/reports  \
           -mindepth 1 -maxdepth 1 -type d  \
           -exec basename {} \;)

report:
	# Building reports...
	@for report in $(REPORTS); do                               \
		$(MAKE) -C . -f $(RTE_SRCDIR)/reports/$$report/Makefile \
			--no-print-directory                                \
			RTE_OUTPUT=$(RTE_OUTPUT)/$$report                   \
			UNITTEST=1                                          \
			build_report                                        \
	; done
	# Running reports
	@for report in $(REPORTS); do                              \
		sudo $(RTE_OUTPUT)/$$report/report                     \
                  && echo "[OK] $$report" ||                   \
                           echo "[FAIL] $$report (status=$$?)" \
	; done
