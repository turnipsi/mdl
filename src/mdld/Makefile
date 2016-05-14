# $Id: Makefile,v 1.3 2016/05/14 20:27:19 je Exp $

SRCS=	mdld.c

PREFIX?=	/usr/local
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
	mkdir -p ${DESTDIR}${PREFIX}/bin
	install -s mdld ${DESTDIR}${PREFIX}/bin/mdld

.PHONY: clean
clean:
	rm -f afl-mdld mdld
