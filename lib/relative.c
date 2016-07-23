/* $Id: relative.c,v 1.25 2016/07/23 20:31:37 je Exp $ */

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

#include <sys/queue.h>

#include <assert.h>
#include <stdlib.h>

#include "instrument.h"
#include "musicexpr.h"
#include "relative.h"
#include "song.h"
#include "track.h"
#include "util.h"

struct previous_relative_exprs {
	struct absdrum	absdrum;
	struct absnote	absnote;
	float		length;
	enum chordtype	chordtype;
};

static void	relative_to_absolute(struct musicexpr *,
    struct previous_relative_exprs *, int);
static int	get_notevalue_for_drumsym(enum drumsym);
static int	compare_notesyms(enum notesym, enum notesym);

void
_mdl_musicexpr_relative_to_absolute(struct song *song, struct musicexpr *me,
    int level)
{
	struct previous_relative_exprs prev_relative_exprs;
	struct instrument *instrument;

	_mdl_log(MDLLOG_RELATIVE, level,
	    "converting relative expressions to absolute\n");

	level += 1;

	/* Set default values for the first absolute note (toned). */
	instrument = _mdl_track_get_default_instrument(INSTR_TONED,
	    song->default_tonedtrack);
	if (instrument != NULL) {
		prev_relative_exprs.absnote.instrument = instrument;
	} else {
		prev_relative_exprs.absnote.instrument =
		    _mdl_get_instrument(INSTR_TONED, "acoustic grand");
	}
	assert(prev_relative_exprs.absnote.instrument != NULL);

	prev_relative_exprs.length = 0.25;

	prev_relative_exprs.absnote.notesym = NOTE_C;
	prev_relative_exprs.absnote.note = 60;
	prev_relative_exprs.absnote.track = song->default_tonedtrack;

	/* Set default values for the first absolute note. */
	instrument = _mdl_track_get_default_instrument(INSTR_DRUMKIT,
	    song->default_drumtrack);
	if (instrument != NULL) {
		prev_relative_exprs.absdrum.instrument = instrument;
	} else {
		prev_relative_exprs.absdrum.instrument =
		    _mdl_get_instrument(INSTR_DRUMKIT, "drums");
	}
	assert(prev_relative_exprs.absdrum.instrument != NULL);

	prev_relative_exprs.absdrum.drumsym = DRUM_BD;
	prev_relative_exprs.absdrum.track = song->default_drumtrack;

	/* Set default value for the first chordtype. */
	prev_relative_exprs.chordtype = CHORDTYPE_MAJ;

	relative_to_absolute(me, &prev_relative_exprs, level);
}

static void
relative_to_absolute(struct musicexpr *me,
    struct previous_relative_exprs *prev_exprs, int level)
{
	struct musicexpr *p;
	struct absdrum absdrum;
	struct absnote absnote;
	struct reldrum reldrum;
	struct relnote relnote;
	struct instrument *instrument;
	struct previous_relative_exprs prev_exprs_copy;
	int notevalues[] = {
		/* For NOTE_C, NOTE_D, ... */
		0, 2, 4, 5, 7, 9, 11,
	};
	int first_note_seen, note, c;
	char *me_id;

	if ((me_id = _mdl_musicexpr_id_string(me)) != NULL) {
		_mdl_log(MDLLOG_RELATIVE, level,
		    "rel->abs for expression %s\n", me_id);
		free(me_id);
	}

	level += 1;

	switch (me->me_type) {
	case ME_TYPE_ABSDRUM:
	case ME_TYPE_ABSNOTE:
		/* These music expression types should not occur here. */
		assert(0);
		break;
	case ME_TYPE_CHORD:
		relative_to_absolute(me->u.chord.me, prev_exprs, level);
		if (me->u.chord.chordtype == CHORDTYPE_NONE)
			me->u.chord.chordtype = prev_exprs->chordtype;
		prev_exprs->chordtype = me->u.chord.chordtype;
		break;
	case ME_TYPE_EMPTY:
		break;
	case ME_TYPE_FLATSIMULTENCE:
		if (me->u.flatsimultence.length == 0) {
			me->u.flatsimultence.length = prev_exprs->length;
		} else {
			prev_exprs->length = me->u.flatsimultence.length;
		}
		relative_to_absolute(me->u.flatsimultence.me, prev_exprs,
		    level);
		break;
	case ME_TYPE_JOINEXPR:
		relative_to_absolute(me->u.joinexpr.a, prev_exprs, level);
		relative_to_absolute(me->u.joinexpr.b, prev_exprs, level);
		break;
	case ME_TYPE_NOTEOFFSETEXPR:
		relative_to_absolute(me->u.noteoffsetexpr.me, prev_exprs,
		    level);
		break;
	case ME_TYPE_OFFSETEXPR:
		relative_to_absolute(me->u.offsetexpr.me, prev_exprs, level);
		break;
	case ME_TYPE_ONTRACK:
		prev_exprs_copy = *prev_exprs;
		prev_exprs->absdrum.track = me->u.ontrack.track;
		prev_exprs->absnote.track = me->u.ontrack.track;

		instrument = _mdl_track_get_default_instrument(INSTR_TONED,
		    me->u.ontrack.track);
		if (instrument != NULL)
			prev_exprs->absnote.instrument = instrument;

		instrument = _mdl_track_get_default_instrument(INSTR_DRUMKIT,
		    me->u.ontrack.track);
		if (instrument != NULL)
			prev_exprs->absdrum.instrument = instrument;

		relative_to_absolute(me->u.ontrack.me, prev_exprs, level);
		*prev_exprs = prev_exprs_copy;
		break;
	case ME_TYPE_RELDRUM:
		_mdl_musicexpr_log(me, MDLLOG_RELATIVE, level, NULL);

		reldrum = me->u.reldrum;

		assert(reldrum.drumsym < DRUM_MAX);
		assert(reldrum.length >= 0);

		note = get_notevalue_for_drumsym(reldrum.drumsym);

		absdrum = prev_exprs->absdrum;
		absdrum.length = reldrum.length;
		if (absdrum.length == 0)
			absdrum.length = prev_exprs->length;
		absdrum.note = note;
		absdrum.drumsym = absdrum.drumsym;

		me->me_type = ME_TYPE_ABSDRUM;
		me->u.absdrum = absdrum;

		prev_exprs->absdrum = absdrum;
		prev_exprs->length  = absdrum.length;

		break;
	case ME_TYPE_RELNOTE:
		_mdl_musicexpr_log(me, MDLLOG_RELATIVE, level, NULL);

		relnote = me->u.relnote;

		assert(relnote.notesym < NOTE_MAX);
		assert(relnote.length >= 0);

		note = 12 * (prev_exprs->absnote.note / 12) +
		    notevalues[relnote.notesym] + relnote.notemods;

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
			absnote.length = prev_exprs->length;
		absnote.note = note;
		absnote.notesym = relnote.notesym;

		me->me_type = ME_TYPE_ABSNOTE;
		me->u.absnote = absnote;

		prev_exprs->absnote = absnote;
		prev_exprs->length  = absnote.length;

		break;
	case ME_TYPE_REST:
		_mdl_musicexpr_log(me, MDLLOG_RELATIVE, level, NULL);

		if (me->u.rest.length == 0) {
			me->u.rest.length = prev_exprs->length;
		} else {
			prev_exprs->length = me->u.rest.length;
		}
		break;
	case ME_TYPE_RELSIMULTENCE:
		assert(me->u.scaledexpr.me->me_type == ME_TYPE_SIMULTENCE);

		/*
		 * In case value for scaled expression length is missing,
		 * get it from previous length.
		 */
		if (me->u.scaledexpr.length == 0)
			me->u.scaledexpr.length = prev_exprs->length;

		/*
		 * Order should not generally matter in simultences, except
		 * in this case we let relativity to affect simultence
		 * notes.  The first note should affect relativity
		 * (not default length), but not subsequent ones.
		 */
		first_note_seen = 0;
		prev_exprs_copy = *prev_exprs;
		TAILQ_FOREACH(p, &me->u.scaledexpr.me->u.melist, tq) {
			relative_to_absolute(p, prev_exprs, level);
			if (!first_note_seen)
				prev_exprs_copy = *prev_exprs;
			first_note_seen = 1;
		}
		*prev_exprs = prev_exprs_copy;

		/* We also set default length for subsequent expressions. */
		prev_exprs->length = me->u.scaledexpr.length;

		/*
		 * relsimultence can now be treated like normal
		 * scaled expression.
		 */
		me->me_type = ME_TYPE_SCALEDEXPR;

		break;
	case ME_TYPE_SCALEDEXPR:
		relative_to_absolute(me->u.scaledexpr.me, prev_exprs, level);
		break;
	case ME_TYPE_SEQUENCE:
		/*
		 * Make the first note in a sequence affect notes/lengths
		 * of subsequent expressions that follow the sequence.
		 */
		first_note_seen = 0;
		prev_exprs_copy = *prev_exprs;
		TAILQ_FOREACH(p, &me->u.melist, tq) {
			relative_to_absolute(p, prev_exprs, level);
			if (!first_note_seen)
				prev_exprs_copy = *prev_exprs;
			first_note_seen = 1;
		}
		*prev_exprs = prev_exprs_copy;
		break;
	case ME_TYPE_SIMULTENCE:
		/*
		 * For simultences, make previous expression affect all
		 * expressions in simultence, but do not let expressions
		 * in simultence affect subsequent expressions.
		 */
		prev_exprs_copy = *prev_exprs;
		TAILQ_FOREACH(p, &me->u.melist, tq) {
			prev_exprs_copy = *prev_exprs;
			relative_to_absolute(p, &prev_exprs_copy, level);
		}
		*prev_exprs = prev_exprs_copy;
		break;
	default:
		assert(0);
	}

	if (me->me_type == ME_TYPE_ABSDRUM ||
	    me->me_type == ME_TYPE_ABSNOTE ||
	    me->me_type == ME_TYPE_REST)
		_mdl_musicexpr_log(me, MDLLOG_RELATIVE, level+1, "--> ");
}

static int
get_notevalue_for_drumsym(enum drumsym drumsym)
{
	assert(drumsym < DRUM_MAX);

	/* XXX Should give the real values. */
	return drumsym;
}

/*
 * If equal                                 -->  0.
 * If higher b is closer to a than lower  b -->  1.
 * if lower  b is closer to a than higher b --> -1.
 */
static int
compare_notesyms(enum notesym a, enum notesym b)
{
	int diff;

	assert(a < NOTE_MAX);
	assert(b < NOTE_MAX);

	if (a == b)
		return 0;

	diff = a - b;
	if (diff < 0)
		diff += NOTE_MAX;

	return (diff < 4) ? -1 : 1;
}
