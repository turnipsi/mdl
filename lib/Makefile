# $Id: Makefile,v 1.27 2016/03/22 19:58:07 je Exp $

PROG=	mdl
SRCS=	compat.c interpreter.c instrument.c joinexpr.c mdl.c midi.c \
	midistream.c musicexpr.c parse.y lex.l relative.c sequencer.c song.c \
	track.c util.c

DPADD=	${LIBM} ${LIBSNDIO}
LDADD=	-lm -lsndio

PREFIX ?= /usr/local

BINDIR=	${PREFIX}/bin
MANDIR=	${PREFIX}/man/man

CFLAGS+=	-I${.CURDIR}

COPTS+=	-g -Wall -W -Wpointer-arith -Wbad-function-cast

.if "${SANDBOX}" == "no"
COPTS+=	-DMDL_SANDBOX_NONE
.endif

.include <bsd.prog.mk>
