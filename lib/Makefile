# $Id: Makefile,v 1.58 2016/06/02 18:31:20 je Exp $

SRCS=	interpreter.c instrument.c joinexpr.c lex.c midi.c midistream.c \
	musicexpr.c parse.c protocol.c relative.c sequencer.c song.c track.c \
	util.c

PREFIX?=	/usr/local
MANPREFIX?=	${PREFIX}/man
COMPATDIR?=	../compat

CDIAGFLAGS+=	-W -Wall -Wbad-function-cast -Wpointer-arith -Wswitch-enum
DEBUG?=		-g
CFLAGS?=	-O2 -pipe ${DEBUG}
CFLAGS+=	${CDIAGFLAGS} -I. -I${COMPATDIR} -include ../config.h
PICFLAG?=	-fPIC

AFLCC?=	afl-gcc

include ../config.mk

# XXX -lutil is only for imsg, might not be necessary
LDADD+=	-lm -lutil ${LIBMDL_LDADD}

.PHONY: all
all: libmdl.so

libmdl.so: ${SRCS}
	${CC} ${CFLAGS} ${PICFLAG} -shared -o $@ ${SRCS} ${LDADD}

libmdlafl.so: ${SRCS}
	${AFLCC} ${CFLAGS} ${PICFLAG} -DMDL_USE_AFL -shared -o $@ ${SRCS} \
	    ${LDADD}

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
