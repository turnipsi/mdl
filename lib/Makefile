# $Id: Makefile,v 1.6 2015/10/04 19:39:42 je Exp $

PROG=	mdl
SRCS=	lex.l mdl.c parse.y sequencer.c
DPADD=	${LIBSNDIO}
LDADD=	-lsndio

PREFIX ?= /usr/local

BINDIR=	${PREFIX}/bin
MANDIR=	${PREFIX}/man/man

.include <bsd.prog.mk>
