# $Id: Makefile,v 1.12 2015/10/30 21:07:19 je Exp $

PROG=	mdl
SRCS=	interpreter.c mdl.c midi.c parse.y lex.l sequencer.c
DPADD=	${LIBM} ${LIBSNDIO}
LDADD=	-lm -lsndio

PREFIX ?= /usr/local

BINDIR=	${PREFIX}/bin
MANDIR=	${PREFIX}/man/man

.include <bsd.prog.mk>
