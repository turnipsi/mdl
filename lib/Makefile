# $Id: Makefile,v 1.11 2015/10/28 19:57:06 je Exp $

PROG=	mdl
SRCS=	lex.l interpreter.c mdl.c midi.c parse.y sequencer.c
DPADD=	${LIBM} ${LIBSNDIO}
LDADD=	-lm -lsndio

PREFIX ?= /usr/local

BINDIR=	${PREFIX}/bin
MANDIR=	${PREFIX}/man/man

.include <bsd.prog.mk>
