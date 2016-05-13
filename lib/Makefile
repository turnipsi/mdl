# $Id: Makefile,v 1.56 2016/05/13 20:29:59 je Exp $

SRCS=	interpreter.c instrument.c joinexpr.c lex.c midi.c midistream.c \
	musicexpr.c parse.c protocol.c relative.c sequencer.c song.c track.c \
	util.c

PREFIX?=	/usr/local
COMPATDIR?=	../compat

CDIAGFLAGS+=	-W -Wall -Wbad-function-cast -Wpointer-arith -Wswitch-enum
DEBUG?=		-g
CFLAGS?=	-O2 -pipe ${DEBUG}
CFLAGS+=	${CDIAGFLAGS} -I. -I${COMPATDIR} -include ../config.h
PICFLAG?=	-fPIC

AFLCC?=	afl-gcc

include ../config.mk

LDADD+=	-lm ${LIBMDL_LDADD}

.PHONY: all
all: libmdl.so

libmdl.so: ${SRCS}
	${CC} ${CFLAGS} ${PICFLAG} -shared -o $@ ${SRCS} ${LDADD}

libmdlafl.so: ${SRCS}
	${AFLCC} ${CFLAGS} ${PICFLAG} -DMDL_USE_AFL -shared -o $@ ${SRCS} \
	    ${LDADD}

# XXX What about the *.a files?  Library version numbering?
install:
	mkdir -p ${DESTDIR}${PREFIX}/lib
	install -s libmdl.so ${DESTDIR}${PREFIX}/lib/libmdl.so

lex.c: lex.l
	lex -o lex.c $<

parse.c y.tab.h: parse.y
	yacc -d $<
	mv y.tab.c parse.c

.PHONY: clean
clean:
	rm -f lex.c libmdl.so libmdlafl.so parse.c y.tab.c y.tab.h
