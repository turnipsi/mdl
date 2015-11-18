# $Id: Makefile,v 1.18 2015/11/18 20:18:45 je Exp $

PROG=	mdl
SRCS=	compat.c interpreter.c mdl.c midi.c musicexpr.c parse.y lex.l \
	sequencer.c util.c

DPADD=	${LIBM} ${LIBSNDIO}
LDADD=	-lm -lsndio

PREFIX ?= /usr/local

BINDIR=	${PREFIX}/bin
MANDIR=	${PREFIX}/man/man

COPTS+=	-g -Wall

.include <bsd.prog.mk>
