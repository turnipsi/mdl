# $Id: Makefile,v 1.14 2015/11/01 20:33:08 je Exp $

PROG=	mdl
SRCS=	interpreter.c mdl.c midi.c musicexpr.c parse.y lex.l sequencer.c \
	util.c
DPADD=	${LIBM} ${LIBSNDIO}
LDADD=	-lm -lsndio

PREFIX ?= /usr/local

BINDIR=	${PREFIX}/bin
MANDIR=	${PREFIX}/man/man

.include <bsd.prog.mk>
