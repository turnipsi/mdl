# $Id: Makefile,v 1.31 2016/04/10 19:48:28 je Exp $

PROG=	mdl
SRCS=	compat.c interpreter.c instrument.c joinexpr.c mdl.c midi.c \
	midistream.c musicexpr.c parse.y lex.l relative.c sequencer.c song.c \
	track.c util.c
MAN=	mdl.1 mdl.5

DPADD=	${LIBM} ${LIBSNDIO}
LDADD=	-lm -lsndio

PREFIX ?= /usr/local

BINDIR=	${PREFIX}/bin
MANDIR=	${PREFIX}/man/man

CFLAGS+=	-I${.CURDIR}

COPTS+=	-g -W -Wall -Wbad-function-cast -Wpointer-arith -Wswitch-enum

.if "${MDL_USE_AFL}" == "1"
AFLCC?=	afl-gcc

NOMAN=	noman
CC=	${AFLCC}
COPTS+=	-DMDL_USE_AFL
.endif

.PHONY: update-compat
update-compat: compat.c.m4 compat.h.m4
	m4 ${.CURDIR}/compat.c.m4 > ${.CURDIR}/compat.c
	m4 ${.CURDIR}/compat.h.m4 > ${.CURDIR}/compat.h

.include <bsd.prog.mk>
