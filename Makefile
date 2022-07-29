# SPDX-License-Identifier: GPL-2.0
# Top level Makefile for bpf-examples

ifeq ("$(origin V)", "command line")
VERBOSE = $(V)
endif
ifndef VERBOSE
VERBOSE = 0
endif

ifeq ($(VERBOSE),0)
MAKEFLAGS += --no-print-directory
Q = @
endif

SUBDIRS := encap-forward lsm-nobpf pkt-loop-filter
.PHONY: check_submodule help archive clobber distclean clean $(SUBDIRS)

all: lib $(SUBDIRS)

lib: config.mk check_submodule
	@echo; echo $@; $(MAKE) -C $@

$(SUBDIRS):
	@echo; echo $@; $(MAKE) -C $@

help:
	@echo "Make Targets:"
	@echo " all                 - build binaries"
	@echo " clean               - remove products of build"
	@echo " distclean           - remove configuration and build"
	@echo " install             - install binaries on local machine"
	@echo " test                - run test suite"
	@echo " archive             - create tarball of all sources"
	@echo ""
	@echo "Make Arguments:"
	@echo " V=[0|1]             - set build verbosity level"

config.mk: configure
	sh configure

check_submodule:
	@if [ -d .git ] && `git submodule status lib/libbpf | grep -q '^+'`; then \
		echo "" ;\
		echo "** WARNING **: git submodule SHA-1 out-of-sync" ;\
		echo " consider running: git submodule update"  ;\
		echo "" ;\
	fi\

archive: distclean
	$(eval GITSHASH := $(shell git log --format=%h HEAD^..HEAD))
	$(eval GITHASH  := $(shell git log --format=%H HEAD^..HEAD))
	git archive --format=tar.gz --prefix=bpf-examples-$(GITSHASH)/ -o ./bpf-examples-$(GITSHASH).tar.gz $(GITHASH)

clobber:
	touch config.mk
	$(MAKE) clean
	rm -f config.mk cscope.* compile_commands.json

distclean: clobber
	$(Q)rm -f *.tar.gz

clean: check_submodule
	$(Q)for i in $(SUBDIRS); \
	do $(MAKE) -C $$i clean; done
	$(Q)$(MAKE) -C lib clean

compile_commands.json: clean
	compiledb make V=1
