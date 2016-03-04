# $Id: Makefile,v 1.2 2016/03/04 20:39:59 je Exp $

MDL?=	mdl

REGRESS_TARGETS=t-empty t-simple1 t-simple2

.SUFFIXES: .in

#   .in: input file
# .*.ok: desired result

.in:
	@echo ${*}
	@${MDL} -d parsing ${.CURDIR}/${*}.in > ${*}.parsing.log
	@cmp -s ${.CURDIR}/${*}.parsing.ok ${*}.parsing.log \
		|| { diff -u ${.CURDIR}/${*}.parsing.ok \
			     ${.OBJDIR}/${*}.parsing.log; \
		     false; }

.PHONY: ${REGRESS_TARGETS}

CLEANFILES+=*.log

.include <bsd.regress.mk>
