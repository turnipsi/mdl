# $Id: Makefile,v 1.4 2016/03/05 20:37:33 je Exp $

MDL?=	${.CURDIR}/../src/mdl

MIDI_TESTS=t-empty
MIDISTREAM_TESTS=t-empty t-simple1
PARSING_TESTS=t-empty t-simple1 t-simple2
RELATIVE_TESTS=t-empty t-simple1 t-simple2

REGRESS_TARGETS=${PARSING_TESTS:S/$/.parsing/}       \
		${RELATIVE_TESTS:S/$/.relative/}     \
		${MIDISTREAM_TESTS:S/$/.midistream/} \
		${MIDI_TESTS:S/$/.midi/}

DEBUGOPTS=midi midistream parsing relative

.SUFFIXES: .in ${DEBUGOPTS:S/^/./}

# .in: input file
# .${DEBUGOPT}.ok: desired result
.for DEBUGOPT in ${DEBUGOPTS}
  .in.${DEBUGOPT}:
	@echo $@
	@${MDL} -d ${DEBUGOPT} ${.CURDIR}/$*.in > $@.out
	@cmp -s ${.CURDIR}/$@.ok $@.out \
		|| { diff -u ${.CURDIR}/$@.ok ${.OBJDIR}/$@.out; false; }
.endfor

.PHONY: ${REGRESS_TARGETS}

CLEANFILES+=*.out

.include <bsd.regress.mk>
