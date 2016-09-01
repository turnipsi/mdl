# $Id: Makefile,v 1.6 2016/09/01 18:40:43 je Exp $

SRCS=	mdld.c

PREFIX?=	/usr/local
MANPREFIX?=	${PREFIX}/man
COMPATDIR?=	../../compat

LIBMDL_PATH?=	../../lib

CDIAGFLAGS?=	-W -Wall -Wbad-function-cast -Wpointer-arith
CFLAGS?=	-O2 -pipe
DEBUG?=		-g
PICFLAG?=	-fPIC

CC_ARGS=	${CFLAGS} ${DEBUG} ${CDIAGFLAGS} ${PICFLAG} -I${LIBMDL_PATH} \
		-I${COMPATDIR} -include ../config.h

AFLCC?=	afl-gcc

include ../../config.mk

.PHONY: all
all: mdld

mdld: ${SRCS}
	(cd ${LIBMDL_PATH} && ${MAKE} all)
	${CC} ${CC_ARGS} -L${LIBMDL_PATH} -o $@ ${SRCS} ${LDADD} -lmdl

afl-mdld: ${SRCS}
	(cd ${LIBMDL_PATH} && ${MAKE} libmdlafl.so)
	${AFLCC} ${CC_ARGS} -L${LIBMDL_PATH} -o $@ ${SRCS} ${LDADD} -lmdlafl

.PHONY: install
install: mdld
	mkdir -p ${DESTDIR}${PREFIX}/bin ${DESTDIR}${MANPREFIX}/man1
	install -s mdld ${DESTDIR}${PREFIX}/bin/mdld
	install mdld.1 ${DESTDIR}${MANPREFIX}/man1/mdld.1

.PHONY: clean
clean:
	rm -f afl-mdld mdld
