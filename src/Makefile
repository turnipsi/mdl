# $Id: Makefile,v 1.1 2016/05/09 20:35:57 je Exp $

SUBDIRS = mdl mdld

all clean:
	for subdir in ${SUBDIRS}; do \
	    (cd $$subdir && ${MAKE} $@); \
	done
