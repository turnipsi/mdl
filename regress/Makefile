# $Id: Makefile,v 1.32 2016/09/21 20:08:55 je Exp $

LIBDIR?=	../lib
SRCDIR?=	../src

AFLMDL=		${SRCDIR}/mdl/afl-mdl

.PHONY: all
all:
	${MAKE} -C ${SRCDIR}/mdl mdl
	@./run-tests

.PHONY: accept-regressions
accept-regressions:
	for log in outputs/*.log; do \
	    test -e "$$log" || continue; \
	    mv "$$log" "expected/$$(basename $$log .log).ok"; \
	done

.PHONY: afl-fuzz
afl-fuzz: afl-fuzz-inputs ${AFLMDL}
	env LD_LIBRARY_PATH=${LIBDIR} afl-fuzz -i afl-fuzz-inputs \
	    -o afl-fuzz-results ${AFLMDL} -n @@

afl-fuzz-inputs: ${AFLMDL} inputs
	env LD_LIBRARY_PATH=${LIBDIR} afl-cmin -i inputs \
	    -o afl-fuzz-inputs.tmp -- ${AFLMDL}
	mv afl-fuzz-inputs.tmp afl-fuzz-inputs

${AFLMDL}:
	${MAKE} -C ${SRCDIR}/mdl afl-mdl

.PHONY: clean
clean:
	rm -rf afl-fuzz-inputs afl-fuzz-inputs.tmp afl-fuzz-results outputs
