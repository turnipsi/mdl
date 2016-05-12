# $Id: Makefile,v 1.30 2016/05/12 19:54:02 je Exp $

LIBDIR?=	../lib
SRCDIR?=	../src

AFLMDL=		${SRCDIR}/mdl/afl-mdl

.PHONY: all
all:
	${MAKE} -C ${SRCDIR}/mdl clean mdl
	@./run-tests

.PHONY: accept-regressions
accept-regressions:
	for log in outputs/*.log; do \
	    test -e "$$log" || continue; \
	    mv "$$log" "expected/$$(basename $$log .log).ok"; \
	done

.PHONY: afl-fuzz
afl-fuzz:
	${MAKE} -C ${SRCDIR}/mdl clean afl-mdl
	env LD_LIBRARY_PATH=${LIBDIR} afl-fuzz -i inputs \
	    -o afl-fuzz-results ${AFLMDL} -n @@

.PHONY: clean
clean:
	rm -rf afl-fuzz-results outputs
