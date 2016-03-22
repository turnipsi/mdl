# $Id: Makefile,v 1.9 2016/03/22 10:42:13 je Exp $

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
		t-some-extended-lengths        \
		t-unjoinable-chords-joining    \
		t-unjoinable-notes-joining

DEBUGOPTS=parsing relative midistream

REGRESS_TARGETS=
.for TEST_INPUT in ${TEST_INPUTS}
  REGRESS_TARGETS+=${TEST_INPUT:S/$/.parsing/}
  REGRESS_TARGETS+=${TEST_INPUT:S/$/.relative/}
  REGRESS_TARGETS+=${TEST_INPUT:S/$/.midistream/}
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
