# $Id: Makefile,v 1.16 2015/11/09 20:15:07 je Exp $

PROG=	mdl
SRCS=	compat.c interpreter.c mdl.c midi.c musicexpr.c parse.y lex.l \
	sequencer.c

DPADD=	${LIBM} ${LIBSNDIO}
LDADD=	-lm -lsndio

PREFIX ?= /usr/local

BINDIR=	${PREFIX}/bin
MANDIR=	${PREFIX}/man/man

COPTS+=	-Wall

.include <bsd.prog.mk>
