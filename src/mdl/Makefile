# $Id: Makefile,v 1.3 2016/05/10 09:12:52 je Exp $

SRCS=	mdl.c

PREFIX?=	/usr/local
COMPATDIR?=	../../compat

LIBMDL_PATH?=	../../lib

CDIAGFLAGS+=	-W -Wall -Wbad-function-cast -Wpointer-arith -Wswitch-enum
CFLAGS?=	-O2 -pipe
CFLAGS+=	${CDIAGFLAGS} -I${LIBMDL_PATH} -I${COMPATDIR} \
		    -include ../../config.h

AFLCC?=	afl-gcc

include ../../config.mk

LDADD+=	-lmdl

.PHONY: all
all: mdl

mdl: ${SRCS}
	(cd ${LIBMDL_PATH} && ${MAKE} all)
	${CC} ${CFLAGS} -L${LIBMDL_PATH} -o $@ ${SRCS} ${LDADD}

.PHONY: install
install: mdl
	mkdir -p ${DESTDIR}${PREFIX}/bin
	install mdl ${DESTDIR}${PREFIX}/bin/mdl

.PHONY: clean
clean:
	rm -f mdl
