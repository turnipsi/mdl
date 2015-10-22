# $Id: Makefile,v 1.8 2015/10/22 19:03:22 je Exp $

PROG=	mdl
SRCS=	lex.l interpreter.c mdl.c parse.y sequencer.c
DPADD=	${LIBM} ${LIBSNDIO}
LDADD=	-lm -lsndio

PREFIX ?= /usr/local

BINDIR=	${PREFIX}/bin
MANDIR=	${PREFIX}/man/man

.include <bsd.prog.mk>
