# $Id: Makefile,v 1.51 2016/05/10 08:32:22 je Exp $

SRCS=	interpreter.c instrument.c joinexpr.c lex.c mdl.c midi.c midistream.c \
	musicexpr.c parse.c relative.c sequencer.c song.c track.c util.c

CDIAGFLAGS+=	-W -Wall -Wbad-function-cast -Wpointer-arith -Wswitch-enum
CFLAGS?=	-O2 -pipe
CFLAGS+=	${CDIAGFLAGS} -I. -Icompat -include ../config.h
PICFLAG?=	-fPIC

LDADD+=	-lm

AFLCC?=	afl-gcc

COMPATDIR?=	../compat

include ../config.mk

.PHONY: all
all: libmdl.so

libmdl.so: ${SRCS}
	${CC} ${CFLAGS} ${PICFLAG} -shared -o $@ ${SRCS} ${LDADD}

libmdlafl.so: ${SRCS}
	${AFLCC} ${CFLAGS} ${PICFLAG} -DMDL_USE_AFL -shared -o $@ ${SRCS} \
	    ${LDADD}

lex.c: lex.l
	lex -o lex.c $<

parse.c y.tab.h: parse.y
	yacc -d $<
	mv y.tab.c parse.c

.PHONY: clean
clean:
	rm -f lex.c libmdl.so libmdlafl.so parse.c y.tab.c y.tab.h
