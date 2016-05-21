# $Id: Makefile,v 1.4 2016/05/21 19:38:24 je Exp $

SRCS=	mdld.c

PREFIX?=	/usr/local
MANPREFIX?=	${PREFIX}/man
COMPATDIR?=	../../compat

LIBMDL_PATH?=	../../lib

CDIAGFLAGS+=	-W -Wall -Wbad-function-cast -Wpointer-arith -Wswitch-enum
DEBUG?=		-g
CFLAGS?=	-O2 -pipe ${DEBUG}
CFLAGS+=	${CDIAGFLAGS} -I${LIBMDL_PATH} -I${COMPATDIR} \
		    -include ../../config.h

AFLCC?=	afl-gcc

include ../../config.mk

.PHONY: all
all: mdld

mdld: ${SRCS}
	(cd ${LIBMDL_PATH} && ${MAKE} all)
	${CC} ${CFLAGS} -L${LIBMDL_PATH} -o $@ ${SRCS} ${LDADD} -lmdl

afl-mdld: ${SRCS}
	(cd ${LIBMDL_PATH} && ${MAKE} libmdlafl.so)
	${AFLCC} ${CFLAGS} -L${LIBMDL_PATH} -o $@ ${SRCS} ${LDADD} -lmdlafl

.PHONY: install
install: mdld
	mkdir -p ${DESTDIR}${PREFIX}/bin ${DESTDIR}${MANPREFIX}/man1
	install -s mdld ${DESTDIR}${PREFIX}/bin/mdld
	install mdld.1 ${DESTDIR}${MANPREFIX}/man1/mdld.1

.PHONY: clean
clean:
	rm -f afl-mdld mdld
