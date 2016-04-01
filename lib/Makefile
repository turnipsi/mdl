# $Id: Makefile,v 1.28 2016/04/01 19:59:23 je Exp $

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

.if "${MDL_SANDBOX}" == "1"
COPTS+=	-DMDL_SANDBOX_NONE
.endif

.if "${MDL_USE_AFL}" == "1"
AFLCC?=	afl-gcc

NOMAN=	noman
CC=	${AFLCC}
COPTS+=	-DMDL_USE_AFL
.endif

.include <bsd.prog.mk>
