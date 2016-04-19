# $Id: Makefile,v 1.38 2016/04/19 19:42:40 je Exp $

PROG=	mdl
SRCS=	interpreter.c instrument.c joinexpr.c mdl.c midi.c midistream.c \
	musicexpr.c parse.y lex.l relative.c sequencer.c song.c track.c \
	util.c
MAN=	mdl.1 mdl.5

.PATH: ${.CURDIR}/compat

sinclude ../config.mk

DPADD+=	${LIBM} ${LIBSNDIO}
LDADD+=	-lm -lsndio

PREFIX ?= /usr/local

BINDIR=	${PREFIX}/bin
MANDIR=	${PREFIX}/man/man

CDIAGFLAGS+=	-W -Wall -Wbad-function-cast -Wpointer-arith -Wswitch-enum
CFLAGS+=	-I${.CURDIR}

.if "${MDL_USE_AFL}" == "1"
AFLCC?=	afl-gcc

NOMAN=	noman
CC=	${AFLCC}
COPTS+=	-DMDL_USE_AFL
.endif

.include <bsd.prog.mk>
