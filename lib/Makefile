# $Id: Makefile,v 1.64 2016/09/01 18:40:41 je Exp $

SRCS=	functions.c interpreter.c instrument.c ipc.c joinexpr.c lex.c midi.c \
	midistream.c musicexpr.c parse.c relative.c sequencer.c song.c \
	textloc.c track.c util.c

PREFIX?=	/usr/local
MANPREFIX?=	${PREFIX}/man
COMPATDIR?=	../compat

CDIAGFLAGS?=	-W -Wall -Wbad-function-cast -Wpointer-arith
CFLAGS?=	-O2 -pipe
DEBUG?=		-g
PICFLAG?=	-fPIC

CC_ARGS=	${CFLAGS} ${DEBUG} ${CDIAGFLAGS} ${PICFLAG} -I. \
		-I${COMPATDIR} -include ../config.h

AFLCC?=	afl-gcc

include ../config.mk

LDADD+=	-lm ${LIBMDL_LDADD}

.PHONY: all
all: libmdl.so

libmdl.so: ${SRCS}
	${CC} ${CC_ARGS} -shared -o $@ ${SRCS} ${LDADD}

libmdlafl.so: ${SRCS}
	${AFLCC} ${CC_ARGS} -DMDL_USE_AFL -shared -o $@ ${SRCS} ${LDADD}

# XXX What about the *.a files?  Library version numbering?
install:
	mkdir -p ${DESTDIR}${PREFIX}/lib ${DESTDIR}${MANPREFIX}/man5
	install -s libmdl.so ${DESTDIR}${PREFIX}/lib/libmdl.so
	install mdl.5 ${DESTDIR}${MANPREFIX}/man5/mdl.5

lex.c: lex.l
	lex -o lex.c $<

parse.c y.tab.h: parse.y
	yacc -d $<
	mv y.tab.c parse.c

.PHONY: clean
clean:
	rm -f lex.c libmdl.so libmdlafl.so parse.c y.tab.c y.tab.h
