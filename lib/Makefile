# $Id: Makefile,v 1.13 2015/10/31 21:45:42 je Exp $

PROG=	mdl
SRCS=	interpreter.c mdl.c midi.c musicexpr.c parse.y lex.l sequencer.c
DPADD=	${LIBM} ${LIBSNDIO}
LDADD=	-lm -lsndio

PREFIX ?= /usr/local

BINDIR=	${PREFIX}/bin
MANDIR=	${PREFIX}/man/man

.include <bsd.prog.mk>
