# $Id: Makefile,v 1.22 2016/01/24 21:04:35 je Exp $

PROG=	mdl
SRCS=	compat.c interpreter.c instrument.c joinexpr.c mdl.c midi.c \
	midistream.c musicexpr.c parse.y lex.l relative.c sequencer.c util.c

DPADD=	${LIBM} ${LIBSNDIO}
LDADD=	-lm -lsndio

PREFIX ?= /usr/local

BINDIR=	${PREFIX}/bin
MANDIR=	${PREFIX}/man/man

COPTS+=	-g -Wall

.include <bsd.prog.mk>
