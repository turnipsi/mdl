# $Id: Makefile,v 1.19 2015/12/06 20:41:48 je Exp $

PROG=	mdl
SRCS=	compat.c interpreter.c joinexpr.c mdl.c midi.c musicexpr.c parse.y \
	lex.l sequencer.c util.c

DPADD=	${LIBM} ${LIBSNDIO}
LDADD=	-lm -lsndio

PREFIX ?= /usr/local

BINDIR=	${PREFIX}/bin
MANDIR=	${PREFIX}/man/man

COPTS+=	-g -Wall

.include <bsd.prog.mk>
