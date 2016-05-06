# $Id: Makefile,v 1.46 2016/05/06 19:55:29 je Exp $

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
