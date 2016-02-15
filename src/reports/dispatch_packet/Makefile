REPORT_NAME = dispatch_packet

export APP = report
export SRCS-y = reports/$(REPORT_NAME)/main.c

build_report: all
	$(Q)echo [$(REPORT_NAME)] built!

include $(RTE_SRCDIR)/Makefile
