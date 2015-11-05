# $Id: Makefile,v 1.15 2015/11/05 20:24:51 je Exp $

PROG=	mdl
SRCS=	interpreter.c mdl.c midi.c musicexpr.c parse.y lex.l sequencer.c \
	util.c
DPADD=	${LIBM} ${LIBSNDIO}
LDADD=	-lm -lsndio

PREFIX ?= /usr/local

BINDIR=	${PREFIX}/bin
MANDIR=	${PREFIX}/man/man

COPTS+=	-Wall

.include <bsd.prog.mk>
