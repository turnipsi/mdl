# $Id: Makefile,v 1.16 2016/03/27 20:56:28 je Exp $

.ifndef MDL
.  if exists(${.CURDIR}/../src/obj/mdl)
MDL=	${.CURDIR}/../src/obj/mdl -n
.  else
MDL=	${.CURDIR}/../src/mdl -n
.  endif
.endif

TEST_INPUTS=	t-big-jumps                        \
		t-chord-joining                    \
		t-chordmodifiers                   \
		t-chords-with-big-jumps            \
		t-chords-with-variable-lengths     \
		t-default-length                   \
		t-empty                            \
		t-join-chord-and-note              \
		t-join-chords                      \
		t-join-chords-partially            \
		t-join-note-and-chord              \
		t-join-notes                       \
		t-join-notes-and-rests             \
		t-join-notes-chords-relsimultences \
		t-join-relsimultences              \
		t-length-changes                   \
		t-many-basic-lengths               \
		t-mix-notes-and-chords             \
		t-mix-notes-and-chords-with-jumps  \
		t-mix-notes-chords-relsimultences  \
		t-notes-and-rests                  \
		t-relsimultences                   \
		t-rest                             \
		t-simple-notes                     \
		t-simultence-with-subexpressions   \
		t-single-note                      \
		t-some-extended-lengths            \
		t-subsequence                      \
		t-subsimultence                    \
		t-track-with-expression            \
		t-unjoinable-chords-joining        \
		t-unjoinable-notes-joining


DEBUGOPTS=exprconv joins midi midistream mm parsing relative song

REGRESS_TARGETS=
.for TEST_INPUT in ${TEST_INPUTS}
  REGRESS_TARGETS+=${TEST_INPUT:S/$/.exprconv/}
  REGRESS_TARGETS+=${TEST_INPUT:S/$/.joins/}
  REGRESS_TARGETS+=${TEST_INPUT:S/$/.midi/}
  REGRESS_TARGETS+=${TEST_INPUT:S/$/.midistream/}
  REGRESS_TARGETS+=${TEST_INPUT:S/$/.mm/}
  REGRESS_TARGETS+=${TEST_INPUT:S/$/.parsing/}
  REGRESS_TARGETS+=${TEST_INPUT:S/$/.relative/}
  REGRESS_TARGETS+=${TEST_INPUT:S/$/.song/}
.endfor

.SUFFIXES: .mdl ${DEBUGOPTS:S/^/./}

# .mdl: input file
# .${DEBUGOPT}.ok: desired result
.for DEBUGOPT in ${DEBUGOPTS}
  .mdl.${DEBUGOPT}:
	@echo $@
	@${MDL} -d ${DEBUGOPT} ${.CURDIR}/$*.mdl > $@.log
	@cmp -s ${.CURDIR}/expected/$@.ok $@.log \
		|| { diff -u ${.CURDIR}/expected/$@.ok ${.OBJDIR}/$@.log; \
		     false; }
.endfor

.PHONY: ${REGRESS_TARGETS}

.PHONY: accept-regressions
accept-regressions:
	for log in ${.OBJDIR}/*.log; do \
	    test -e "$$log" || continue; \
	    mv "$$log" "${.CURDIR}/expected/$$(basename $$log .log).ok"; \
	done

CLEANFILES+=*.log

.include <bsd.regress.mk>
