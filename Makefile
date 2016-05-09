# $Id: Makefile,v 1.4 2016/05/09 20:35:55 je Exp $

SUBDIRS = lib src

all clean:
	for subdir in ${SUBDIRS}; do \
	    (cd $$subdir && ${MAKE} $@); \
	done
