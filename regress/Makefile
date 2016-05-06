# $Id: Makefile,v 1.25 2016/05/06 19:58:25 je Exp $

MDL=	${.CURDIR}/../src/mdl

TEST_INPUTS=	t-afl-fuzzed-crash-1               \
		t-afl-fuzzed-crash-2               \
		t-afl-fuzzed-crash-3               \
		t-big-jumps                        \
		t-chord-joining                    \
		t-chordmodifiers                   \
		t-chords-with-big-jumps            \
		t-chords-with-variable-lengths     \
		t-comments                         \
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
.PATH.mdl: ${.CURDIR}/inputs

outputs:
	@mkdir -p $@

# .mdl: input file
# .${DEBUGOPT}.ok: desired result
.for DEBUGOPT in ${DEBUGOPTS}
  .mdl.${DEBUGOPT}: outputs
	@echo $@
	@${MDL} -d ${DEBUGOPT} -n ${.CURDIR}/inputs/$*.mdl > outputs/$@.log
	@cmp -s ${.CURDIR}/expected/$@.ok outputs/$@.log \
		|| { diff -u ${.CURDIR}/expected/$@.ok \
			 ${.OBJDIR}/outputs/$@.log; \
		     false; }
.endfor

.PHONY: ${REGRESS_TARGETS}

.PHONY: accept-regressions
accept-regressions:
	for log in ${.OBJDIR}/outputs/*.log; do \
	    test -e "$$log" || continue; \
	    mv "$$log" "${.CURDIR}/expected/$$(basename $$log .log).ok"; \
	done

.PHONY: afl-fuzz
afl-fuzz:
	env MAKEOBJDIR=${.CURDIR}/obj ${MAKE} -C ${.CURDIR}/../src \
	    -D MDL_USE_AFL clean all
	afl-fuzz -i ${.CURDIR}/inputs -o afl-fuzz-results \
	    ${.CURDIR}/obj/mdl -n @@

.PHONY: clean
clean:
	rm -rf a.out [Ee]rrs mklog *.core y.tab.h *.c *.o mdl \
	    afl-fuzz-results outputs

.include <bsd.regress.mk>
