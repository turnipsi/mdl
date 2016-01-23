# $Id: Makefile,v 1.21 2016/01/23 19:59:23 je Exp $

PROG=	mdl
SRCS=	compat.c interpreter.c joinexpr.c mdl.c midi.c midistream.c \
	musicexpr.c parse.y lex.l relative.c sequencer.c util.c

DPADD=	${LIBM} ${LIBSNDIO}
LDADD=	-lm -lsndio

PREFIX ?= /usr/local

BINDIR=	${PREFIX}/bin
MANDIR=	${PREFIX}/man/man

COPTS+=	-g -Wall

.include <bsd.prog.mk>
