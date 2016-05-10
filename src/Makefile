# $Id: Makefile,v 1.2 2016/05/10 09:06:28 je Exp $

SUBDIRS = mdl mdld

all install clean:
	@for subdir in ${SUBDIRS}; do \
	    (echo '>' src/$$subdir '($@)'; cd $$subdir && ${MAKE} $@); \
	done
