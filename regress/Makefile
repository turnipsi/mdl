# $Id: Makefile,v 1.29 2016/05/12 19:47:39 je Exp $

LIBDIR?=	${.CURDIR}/../lib
SRCDIR?=	${.CURDIR}/../src

MDL=		${SRCDIR}/mdl/mdl
AFLMDL=		${SRCDIR}/mdl/afl-mdl

.PHONY: all
all:
	@./run-tests

.PHONY: accept-regressions
accept-regressions:
	for log in outputs/*.log; do \
	    test -e "$$log" || continue; \
	    mv "$$log" "expected/$$(basename $$log .log).ok"; \
	done

.PHONY: afl-fuzz
afl-fuzz:
	${MAKE} -C ${.CURDIR}/../src/mdl clean afl-mdl
	env LD_LIBRARY_PATH=${LIBDIR} afl-fuzz -i inputs \
	    -o afl-fuzz-results ${AFLMDL} -n @@

.PHONY: clean
clean:
	rm -rf afl-fuzz-results outputs

.include <bsd.regress.mk>
