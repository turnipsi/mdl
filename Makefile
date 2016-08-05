# $Id: Makefile,v 1.7 2016/08/05 20:20:54 je Exp $

SUBDIRS = lib src

all install clean:
	@for subdir in ${SUBDIRS}; do \
	    (echo '>' $$subdir '($@)'; cd $$subdir && ${MAKE} $@); \
	done

.PHONY: tags
tags:
	find . '(' -name '*.c' -o -name '*.l' -o -name '*.y' ')' \
	    -a ! -name lex.c -a ! -name parse.c \
	    | xargs ctags -d
