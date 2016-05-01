# $Id: Makefile,v 1.43 2016/05/01 19:58:53 je Exp $

PROG=	mdl
SRCS=	interpreter.c instrument.c joinexpr.c mdl.c midi.c midistream.c \
	musicexpr.c parse.c lex.l relative.c sequencer.c song.c track.c \
	util.c
MAN=	mdl.1 mdl.5

.PATH: ${.CURDIR}/compat

include config.mk

DPADD+=	${LIBM} ${LIBSNDIO}
LDADD+=	-lm

PREFIX ?= /usr/local

BINDIR=	${PREFIX}/bin
MANDIR=	${PREFIX}/man/man

CDIAGFLAGS+=	-W -Wall -Wbad-function-cast -Wpointer-arith -Wswitch-enum
CFLAGS+=	${CDIAGFLAGS} -I${.CURDIR} -I${.CURDIR}/compat

COPTS+=		-include config.h

.if "${MDL_USE_AFL}" == "1"
AFLCC?=	afl-gcc

NOMAN=	noman
CC=	${AFLCC}
COPTS+=	-DMDL_USE_AFL
.endif

parse.c y.tab.h: parse.y
	yacc -d $>
	mv y.tab.c parse.c

CLEANFILES+=	parse.c

.include <bsd.prog.mk>
