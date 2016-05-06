# $Id: Makefile,v 1.45 2016/05/06 19:54:05 je Exp $

SRCS=	interpreter.c instrument.c joinexpr.c lex.c mdl.c midi.c midistream.c \
	musicexpr.c parse.c relative.c sequencer.c song.c track.c util.c

CDIAGFLAGS+=	-W -Wall -Wbad-function-cast -Wpointer-arith -Wswitch-enum
CFLAGS?=	-O2 -pipe
CFLAGS+=	${CDIAGFLAGS} -I. -Icompat -include config.h

LDADD+=		-lm

include config.mk

.PHONY: all
all: mdl

mdl: ${SRCS}
	${CC} ${CFLAGS} -o mdl ${SRCS} ${LDADD}

lex.c: lex.l
	lex -o lex.c $<

parse.c y.tab.h: parse.y
	yacc -d $<
	mv y.tab.c parse.c

.PHONY: clean
clean:
	rm -f mdl lex.c parse.c y.tab.c y.tab.h


#
#
#PROG=	mdl
#SRCS=	interpreter.c instrument.c joinexpr.c mdl.c midi.c midistream.c \
#	musicexpr.c parse.c lex.l relative.c sequencer.c song.c track.c \
#	util.c
#MAN=	mdl.1 mdl.5
#
#.PATH: ${.CURDIR}/compat
#
#
#DPADD+=	${LIBM} ${LIBSNDIO}
#LDADD+=	-lm
#
#PREFIX ?= /usr/local
#
#BINDIR=	${PREFIX}/bin
#MANDIR=	${PREFIX}/man/man
#
#CDIAGFLAGS+=	-W -Wall -Wbad-function-cast -Wpointer-arith -Wswitch-enum
#CFLAGS+=	${CDIAGFLAGS} -I${.CURDIR} -I${.CURDIR}/compat
#
#COPTS+=		-include config.h
#
#.if "${MDL_USE_AFL}" == "1"
#AFLCC?=	afl-gcc
#
#NOMAN=	noman
#CC=	${AFLCC}
#COPTS+=	-DMDL_USE_AFL
#.endif
#
#parse.c y.tab.h: parse.y
#	yacc -d $>
#	mv y.tab.c parse.c
#
#CLEANFILES+=	parse.c
#
## Some BSD makes run with gcc flag -Werror, unlike OpenBSD.
## Turn that off.
#NOGCCERROR=	true
#
#.include <bsd.prog.mk>
