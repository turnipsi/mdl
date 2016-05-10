# $Id: Makefile,v 1.5 2016/05/10 09:06:26 je Exp $

SUBDIRS = lib src

all install clean:
	@for subdir in ${SUBDIRS}; do \
	    (echo '>' $$subdir '($@)'; cd $$subdir && ${MAKE} $@); \
	done
