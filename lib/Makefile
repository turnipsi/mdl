# $Id: Makefile,v 1.20 2016/01/23 19:15:42 je Exp $

PROG=	mdl
SRCS=	compat.c interpreter.c joinexpr.c mdl.c midi.c midistream.c \
	musicexpr.c parse.y lex.l sequencer.c util.c

DPADD=	${LIBM} ${LIBSNDIO}
LDADD=	-lm -lsndio

PREFIX ?= /usr/local

BINDIR=	${PREFIX}/bin
MANDIR=	${PREFIX}/man/man

COPTS+=	-g -Wall

.include <bsd.prog.mk>
