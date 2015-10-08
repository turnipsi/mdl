# $Id: Makefile,v 1.7 2015/10/08 19:36:29 je Exp $

PROG=	mdl
SRCS=	lex.l interpreter.c mdl.c parse.y sequencer.c
DPADD=	${LIBSNDIO}
LDADD=	-lsndio

PREFIX ?= /usr/local

BINDIR=	${PREFIX}/bin
MANDIR=	${PREFIX}/man/man

.include <bsd.prog.mk>
