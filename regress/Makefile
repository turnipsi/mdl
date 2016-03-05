# $Id: Makefile,v 1.3 2016/03/05 13:26:47 je Exp $

MDL?=	mdl

REGRESS_TARGETS=t-empty.parsing t-simple1.parsing t-simple2.parsing

.SUFFIXES: .in .parsing

#   .in: input file
# .*.ok: desired result

.in.parsing:
	@echo $@
	@${MDL} -d parsing ${.CURDIR}/$*.in > $@.out
	@cmp -s ${.CURDIR}/$@.ok $@.out \
		|| { diff -u ${.CURDIR}/$@.ok ${.OBJDIR}/$@.out; false; }

.PHONY: ${REGRESS_TARGETS}

CLEANFILES+=*.out

.include <bsd.regress.mk>
