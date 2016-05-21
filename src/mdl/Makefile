# $Id: Makefile,v 1.5 2016/05/21 19:38:23 je Exp $

SRCS=	mdl.c

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
all: mdl

mdl: ${SRCS}
	(cd ${LIBMDL_PATH} && ${MAKE} all)
	${CC} ${CFLAGS} -L${LIBMDL_PATH} -o $@ ${SRCS} ${LDADD} -lmdl

afl-mdl: ${SRCS}
	(cd ${LIBMDL_PATH} && ${MAKE} libmdlafl.so)
	${AFLCC} ${CFLAGS} -L${LIBMDL_PATH} -o $@ ${SRCS} ${LDADD} -lmdlafl

.PHONY: install
install: mdl
	mkdir -p ${DESTDIR}${PREFIX}/bin ${DESTDIR}${MANPREFIX}/man1
	install -s mdl ${DESTDIR}${PREFIX}/bin/mdl
	install mdl.1 ${DESTDIR}${MANPREFIX}/man1/mdl.1

.PHONY: clean
clean:
	rm -f afl-mdl mdl
