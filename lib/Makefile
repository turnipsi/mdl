# $Id: Makefile,v 1.25 2016/02/13 19:59:33 je Exp $

PROG=	mdl
SRCS=	compat.c interpreter.c instrument.c joinexpr.c mdl.c midi.c \
	midistream.c musicexpr.c parse.y lex.l relative.c sequencer.c song.c \
	track.c util.c

DPADD=	${LIBM} ${LIBSNDIO}
LDADD=	-lm -lsndio

PREFIX ?= /usr/local

BINDIR=	${PREFIX}/bin
MANDIR=	${PREFIX}/man/man

COPTS+=	-g -Wall -W -Wpointer-arith -Wbad-function-cast

.include <bsd.prog.mk>
