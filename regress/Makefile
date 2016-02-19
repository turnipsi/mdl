# $Id: Makefile,v 1.1 2016/02/19 21:10:32 je Exp $

MDL?=	mdl

REGRESS_TARGETS=t-empty t-simple1

# .in: input file
# .ok: desired result

all: ${REGRESS_TARGET}

.SUFFIXES: .in

.in:
	@echo ${*}
	@${MDL} -d 4 ${.CURDIR}/${*}.in > ${*}.log
	@cmp -s ${.CURDIR}/${*}.ok ${*}.log || \
		(echo "XXX ${*} failed" && false)

CLEANFILES+=*.log

.include <bsd.regress.mk>
