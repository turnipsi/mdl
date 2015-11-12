# $Id: Makefile,v 1.17 2015/11/12 20:26:57 je Exp $

PROG=	mdl
SRCS=	compat.c interpreter.c mdl.c midi.c musicexpr.c parse.y lex.l \
	sequencer.c

DPADD=	${LIBM} ${LIBSNDIO}
LDADD=	-lm -lsndio

PREFIX ?= /usr/local

BINDIR=	${PREFIX}/bin
MANDIR=	${PREFIX}/man/man

COPTS+=	-g -Wall

.include <bsd.prog.mk>
