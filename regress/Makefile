# $Id: Makefile,v 1.6 2016/03/20 21:11:37 je Exp $

MDL?=	${.CURDIR}/../src/mdl -n

TEST_INPUTS=	t-big-jumps                    \
		t-chord-joining                \
		t-chordmodifiers               \
		t-chords-with-big-jumps        \
		t-chords-with-variable-lengths \
		t-empty                        \
		t-join-notes                   \
		t-join-notes-and-rests         \
		t-length-changes               \
		t-many-basic-lengths           \
		t-notes-and-rests              \
		t-rest                         \
		t-simple-notes                 \
		t-single-note                  \
		t-some-extended-lengths

# XXX t-unjoinable-chords-joining
# XXX t-unjoinable-notes-joining

DEBUGOPTS=parsing relative midistream

REGRESS_TARGETS=
.for TEST_INPUT in ${TEST_INPUTS}
  REGRESS_TARGETS+=${TEST_INPUT:S/$/.parsing/}
  REGRESS_TARGETS+=${TEST_INPUT:S/$/.relative/}
  REGRESS_TARGETS+=${TEST_INPUT:S/$/.midistream/}
.endfor

.SUFFIXES: .in ${DEBUGOPTS:S/^/./}

# .in: input file
# .${DEBUGOPT}.ok: desired result
.for DEBUGOPT in ${DEBUGOPTS}
  .in.${DEBUGOPT}:
	@echo $@
	@${MDL} -d ${DEBUGOPT} ${.CURDIR}/$*.in > $@.out
	@cmp -s ${.CURDIR}/expected/$@.ok $@.out \
		|| { diff -u ${.CURDIR}/expected/$@.ok ${.OBJDIR}/$@.out; \
		     false; }
.endfor

.PHONY: ${REGRESS_TARGETS}

CLEANFILES+=*.out

.include <bsd.regress.mk>
