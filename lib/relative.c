/* $Id: relative.c,v 1.6 2016/02/03 21:09:27 je Exp $ */

/*
 * Copyright (c) 2015 Juha Erkkilä <je@turnipsi.no-ip.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <assert.h>
#include <sys/queue.h>

#include "instrument.h"
#include "musicexpr.h"
#include "relative.h"
#include "song.h"
#include "track.h"
#include "util.h"

struct previous_relative_exprs_t {
	struct absnote_t absnote;
	enum chordtype_t chordtype;
};

static void	relative_to_absolute(struct musicexpr_t *,
				     struct previous_relative_exprs_t *,
				     int);

static int	compare_notesyms(enum notesym_t, enum notesym_t);

void
musicexpr_relative_to_absolute(struct song_t *song,
			       struct musicexpr_t *me,
			       int level)
{
	struct previous_relative_exprs_t prev_relative_exprs;
	struct instrument_t *instrument;

	mdl_log(2, level, "converting relative expression to absolute\n");

	/* set default values for the first absolute note */
	instrument = track_get_default_instrument(song->default_track);
	if (instrument != NULL) {
		prev_relative_exprs.absnote.instrument = instrument;
	} else {
		prev_relative_exprs.absnote.instrument
		    = get_instrument(INSTR_TONED, "acoustic grand");
	}
	assert(prev_relative_exprs.absnote.instrument != NULL);

	prev_relative_exprs.absnote.length = 0.25;
	prev_relative_exprs.absnote.notesym = NOTE_C;
	prev_relative_exprs.absnote.note = 60;
	prev_relative_exprs.absnote.track = song->default_track;

	/* set default value for the first chordtype */
	prev_relative_exprs.chordtype = CHORDTYPE_MAJ;

	relative_to_absolute(me, &prev_relative_exprs, level + 1);
}

static void
relative_to_absolute(struct musicexpr_t *me,
		     struct previous_relative_exprs_t *prev_exprs,
		     int level)
{
	struct musicexpr_t *p;
	struct absnote_t absnote;
	struct relnote_t relnote;
	struct instrument_t *instrument;
	struct previous_relative_exprs_t prev_exprs_copy;
	int notevalues[] = {
		/* for NOTE_C, NOTE_D, ... */
		0, 2, 4, 5, 7, 9, 11,
	};
	int first_note_seen, note, c;

	mdl_log(3,
		level,
		"rel->abs expression (%s)\n",
		musicexpr_type_to_string(me));

	switch (me->me_type) {
	case ME_TYPE_ABSNOTE:
		/* pass as is, but this affects previous absnote */
		prev_exprs->absnote = me->u.absnote;
		break;
	case ME_TYPE_CHORD:
		relative_to_absolute(me->u.chord.me, prev_exprs, level + 1);
		if (me->u.chord.chordtype == CHORDTYPE_NONE)
			me->u.chord.chordtype = prev_exprs->chordtype;
		prev_exprs->chordtype = me->u.chord.chordtype;
		break;
	case ME_TYPE_EMPTY:
		break;
	case ME_TYPE_JOINEXPR:
		relative_to_absolute(me->u.joinexpr.a, prev_exprs, level + 1);
		relative_to_absolute(me->u.joinexpr.b, prev_exprs, level + 1);
		break;
	case ME_TYPE_OFFSETEXPR:
		relative_to_absolute(me->u.offsetexpr.me,
				     prev_exprs,
				     level + 1);
		break;
	case ME_TYPE_ONTRACK:
		prev_exprs_copy = *prev_exprs;
		prev_exprs->absnote.track = me->u.ontrack.track;
		instrument = track_get_default_instrument(me->u.ontrack.track);
		if (instrument != NULL)
			prev_exprs->absnote.instrument = instrument;
		relative_to_absolute(me->u.ontrack.me, prev_exprs, level + 1);
		*prev_exprs = prev_exprs_copy;
		break;
	case ME_TYPE_RELNOTE:
		musicexpr_log(me, 3, level + 1, NULL);

		relnote = me->u.relnote;

		assert(0 <= relnote.notesym && relnote.notesym < NOTE_MAX);
		assert(relnote.length >= 0);

		note = 12 * (prev_exprs->absnote.note / 12)
			 + notevalues[relnote.notesym]
			 + relnote.notemods;

		c = compare_notesyms(prev_exprs->absnote.notesym,
				     relnote.notesym);
		if (c > 0 && prev_exprs->absnote.note > note) {
			note += 12;
		} else if (c < 0 && prev_exprs->absnote.note < note) {
			note -= 12;
		}

		note += 12 * relnote.octavemods;

		absnote = prev_exprs->absnote;
		absnote.length = relnote.length;
		if (absnote.length == 0)
			absnote.length = prev_exprs->absnote.length;
		absnote.note = note;
		absnote.notesym = relnote.notesym;

		me->me_type = ME_TYPE_ABSNOTE;
		me->u.absnote = absnote;

		prev_exprs->absnote = absnote;

		break;
	case ME_TYPE_REST:
		musicexpr_log(me, 3, level + 1, NULL);

		if (me->u.rest.length == 0) {
			me->u.rest.length = prev_exprs->absnote.length;
		} else {
			prev_exprs->absnote.length = me->u.rest.length;
		}
		break;
	case ME_TYPE_RELSIMULTENCE:
		assert(me->u.scaledexpr.me->me_type == ME_TYPE_SIMULTENCE);

		/* in case value for scaled expression length is missing,
		 * get it from previous length */
		if (me->u.scaledexpr.length == 0)
			me->u.scaledexpr.length = prev_exprs->absnote.length;

		/* Order should not generally matter in simultences, except
		 * in this case we let relativity to affect simultence
		 * notes.  The first note should affect relativity
		 * (not default length), but not subsequent ones. */
		first_note_seen = 0;
		prev_exprs_copy = *prev_exprs;
		TAILQ_FOREACH(p, &me->u.scaledexpr.me->u.melist, tq) {
			relative_to_absolute(p, prev_exprs, level + 1);
			if (!first_note_seen)
				prev_exprs_copy = *prev_exprs;
			first_note_seen = 1;
		}
		*prev_exprs = prev_exprs_copy;

		/* we also set default length for subsequent expressions */
		prev_exprs->absnote.length = me->u.scaledexpr.length;

		/* relsimultence can now be treated like normal
		 * scaled expression */
		me->me_type = ME_TYPE_SCALEDEXPR;

		break;
	case ME_TYPE_SCALEDEXPR:
		relative_to_absolute(me->u.scaledexpr.me,
				     prev_exprs,
				     level + 1);
		break;
	case ME_TYPE_SEQUENCE:
		/* make the first note in a sequence affect notes/lengths
		 * of subsequent expressions that follow the sequence */
		first_note_seen = 0;
		prev_exprs_copy = *prev_exprs;
		TAILQ_FOREACH(p, &me->u.melist, tq) {
			relative_to_absolute(p, prev_exprs, level + 1);
			if (!first_note_seen)
				prev_exprs_copy = *prev_exprs;
			first_note_seen = 1;
		}
		*prev_exprs = prev_exprs_copy;
		break;
	case ME_TYPE_SIMULTENCE:
		/* For simultences, make previous expression affect all
		 * expressions in simultence, but do not let expressions
		 * in simultence affect subsequent expressions. */
		prev_exprs_copy = *prev_exprs;
		TAILQ_FOREACH(p, &me->u.melist, tq) {
			prev_exprs_copy = *prev_exprs;
			relative_to_absolute(p, &prev_exprs_copy, level + 1);
		}
		*prev_exprs = prev_exprs_copy;
		break;
	default:
		assert(0);
		break;
	}

	if (me->me_type == ME_TYPE_ABSNOTE || me->me_type == ME_TYPE_REST)
		musicexpr_log(me, 3, level + 2, "--> ");
}

/* if equal                            -->  0
 * if higher b is closer to a than lower  b -->  1
 * if lower  b is closer to a than higher b --> -1 */
static int
compare_notesyms(enum notesym_t a, enum notesym_t b)
{
	int diff;

	assert(0 <= a && a < NOTE_MAX);
	assert(0 <= b && b < NOTE_MAX);

	if (a == b)
		return 0;

	diff = a - b;
	if (diff < 0)
		diff += NOTE_MAX;

	return (diff < 4) ? -1 : 1;
}
