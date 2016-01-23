/* $Id: musicexpr.c,v 1.61 2016/01/23 16:54:58 je Exp $ */

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
#include <err.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "joinexpr.h"
#include "midi.h"
#include "musicexpr.h"
#include "util.h"

#define DEFAULT_MIDICHANNEL	0
#define DEFAULT_VELOCITY	80

struct previous_relative_exprs_t {
	struct absnote_t absnote;
	enum chordtype_t chordtype;
};

struct simultence_state {
	float length, length_no_rests, next_offset;
};

static int	musicexpr_clone_melist(struct melist_t *,
				       struct melist_t,
				       int);
static void	musicexpr_relative_to_absolute(struct musicexpr_t *, int);
static void	relative_to_absolute(struct musicexpr_t *,
				     struct previous_relative_exprs_t *,
				     int);
static void	musicexpr_log_chordtype(enum chordtype_t, int, int, char *);
static void	musicexpr_log_melist(struct melist_t, int, int, char *);

static struct mdl_stream *offsetexprstream_new(void);

static struct musicexpr_t *
musicexpr_tq(enum musicexpr_type me_type,
	     int,
	     struct musicexpr_t *,
	     va_list va);

static int
add_musicexpr_to_flat_simultence(struct musicexpr_t *,
				 struct musicexpr_t *,
				 struct simultence_state *,
				 int);

static struct mdl_stream *
offsetexprstream_to_midievents(struct mdl_stream *, int);

static void	apply_noteoffset(struct musicexpr_t *, int, int);
static struct musicexpr_t *musicexpr_scale_in_time(struct musicexpr_t *,
						   float,
						   int);
static float	musicexpr_calc_length(struct musicexpr_t *);
void		musicexpr_stretch_length_by_factor(struct musicexpr_t *,
						   float);

static int
add_musicexpr_to_midievents(struct mdl_stream *,
			    const struct musicexpr_t *,
			    float,
			    int);

static int	compare_notesyms(enum notesym_t, enum notesym_t);
static int	compare_midievents(const void *, const void *);

static struct musicexpr_t *musicexpr_new_empty(void);

struct musicexpr_t *
musicexpr_clone(struct musicexpr_t *me, int level)
{
	struct musicexpr_t *cloned;
	int ret;

	cloned = malloc(sizeof(struct musicexpr_t));
	if (cloned == NULL) {
		warn("malloc failure when cloning musicexpr");
		return NULL;
	}

	musicexpr_copy(cloned, me);

	mdl_log(4,
		level,
		"cloning expression %p (%s)\n",
		me,
		musicexpr_type_to_string(me));
	musicexpr_log(me, 4, level + 1, NULL);

	switch (me->me_type) {
	case ME_TYPE_ABSNOTE:
	case ME_TYPE_EMPTY:
	case ME_TYPE_RELNOTE:
	case ME_TYPE_REST:
		break;
	case ME_TYPE_CHORD:
		cloned->u.chord.me = musicexpr_clone(me->u.chord.me,
						     level + 1);
		if (cloned->u.chord.me == NULL) {
			free(cloned);
			cloned = NULL;
		}
		break;
	case ME_TYPE_JOINEXPR:
		cloned->u.joinexpr.a = musicexpr_clone(me->u.joinexpr.a,
						       level + 1);
		if (cloned->u.joinexpr.a == NULL) {
			free(cloned);
			cloned = NULL;
			break;
		}
		cloned->u.joinexpr.b = musicexpr_clone(me->u.joinexpr.b,
						       level + 1);
		if (cloned->u.joinexpr.b == NULL) {
			musicexpr_free(cloned->u.joinexpr.a);
			free(cloned);
			cloned = NULL;
		}
		break;
	case ME_TYPE_NOTEOFFSETEXPR:
		cloned->u.noteoffsetexpr.me
		    = musicexpr_clone(me->u.noteoffsetexpr.me, level + 1);
		if (cloned->u.noteoffsetexpr.me == NULL) {
			free(cloned);
			cloned = NULL;
			break;
		}
		cloned->u.noteoffsetexpr.offsets
		    = malloc(me->u.noteoffsetexpr.count);
		if (cloned->u.noteoffsetexpr.offsets == NULL) {
			musicexpr_free(cloned->u.noteoffsetexpr.me);
			free(cloned);
			cloned = NULL;
			break;
		}
		memcpy(&cloned->u.noteoffsetexpr.offsets,
		       &me->u.noteoffsetexpr.offsets,
		       me->u.noteoffsetexpr.count * sizeof(int));
		break;
	case ME_TYPE_RELSIMULTENCE:
	case ME_TYPE_SCALEDEXPR:
		cloned->u.scaledexpr.me = musicexpr_clone(me->u.scaledexpr.me,
							  level + 1);
		if (cloned->u.scaledexpr.me == NULL) {
			free(cloned);
			cloned = NULL;
		}
		break;
	case ME_TYPE_SEQUENCE:
	case ME_TYPE_SIMULTENCE:
		ret = musicexpr_clone_melist(&cloned->u.melist,
					     me->u.melist,
					     level + 1);
		if (ret != 0) {
			free(cloned);
			cloned = NULL;
		}
		break;
	case ME_TYPE_WITHOFFSET:
		cloned->u.offsetexpr.me = musicexpr_clone(me->u.offsetexpr.me,
							  level + 1);
		if (cloned->u.offsetexpr.me == NULL) {
			free(cloned);
			cloned = NULL;
		}
		break;
	default:
		assert(0);
	}

	return cloned;
}

static int
musicexpr_clone_melist(struct melist_t *cloned_melist,
		       struct melist_t melist,
		       int level)
{
	struct musicexpr_t *p, *q;

	TAILQ_INIT(cloned_melist);

	TAILQ_FOREACH(p, &melist, tq) {
		q = musicexpr_clone(p, level);
		if (q == NULL) {
			warnx("cloud not clone music expression list");
			musicexpr_free_melist(*cloned_melist);
			return 1;
		}
		TAILQ_INSERT_TAIL(cloned_melist, q, tq);
	}

	return 0;
}

static void
musicexpr_relative_to_absolute(struct musicexpr_t *me, int level)
{
	struct previous_relative_exprs_t prev_relative_exprs;

	mdl_log(2, level, "converting relative expression to absolute\n");

	/* set default values for the first absolute note */
	prev_relative_exprs.absnote.length = 0.25;
	prev_relative_exprs.absnote.notesym = NOTE_C;
	prev_relative_exprs.absnote.note = 60;

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

		absnote.notesym = relnote.notesym;
		absnote.length  = relnote.length;
		if (absnote.length == 0)
			absnote.length = prev_exprs->absnote.length;
		absnote.note = note;

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
		TAILQ_FOREACH(p, &me->u.melist, tq)
			relative_to_absolute(p, prev_exprs, level + 1);
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
	case ME_TYPE_WITHOFFSET:
		relative_to_absolute(me->u.offsetexpr.me,
				     prev_exprs,
				     level + 1);
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

struct mdl_stream *
musicexpr_to_midievents(struct musicexpr_t *me, int level)
{
	struct musicexpr_t *me_workcopy, *simultence, *p;
	struct mdl_stream *offset_es, *midi_es;

	mdl_log(1, level, "converting music expression to midi stream\n");

	midi_es = NULL;
	simultence = NULL;

	if ((offset_es = offsetexprstream_new()) == NULL) {
		warnx("could not setup new offsetexprstream");
		return NULL;
	}

	if ((me_workcopy = musicexpr_clone(me, level + 1)) == NULL) {
		warnx("could not clone music expressions");
		mdl_stream_free(offset_es);
		return NULL;
	}

	/* first convert relative->absolute,
	 * joinexpr_musicexpr() can not handle relative expressions */
	musicexpr_relative_to_absolute(me_workcopy, level + 1);

	mdl_log(1, level + 1, "joining all music expressions\n");
	if (joinexpr_musicexpr(me_workcopy, level + 1) != 0) {
		warnx("error occurred in joining music expressions");
		goto finish;
	}

	mdl_log(1,
		level + 1,
		"converting expression to a (flat) simultence\n");
	simultence = musicexpr_to_flat_simultence(me_workcopy, level + 1);
	if (simultence == NULL) {
		warnx("Could not flatten music expression to create offset" \
			" expression stream");
		goto finish;
	}

	mdl_log(1, level + 1, "making offset expression stream\n");
	TAILQ_FOREACH(p, &simultence->u.melist, tq) {
		assert(p->me_type == ME_TYPE_WITHOFFSET);
		offset_es->mexprs[ offset_es->count ] = p->u.offsetexpr;
		if (mdl_stream_increment(offset_es) != 0)
			goto finish;
	}

	midi_es = offsetexprstream_to_midievents(offset_es, level + 1);
	if (midi_es == NULL)
		warnx("could not convert offset-expression-stream" \
			" to midistream");

finish:
	mdl_stream_free(offset_es);

	if (simultence != NULL)
		musicexpr_free(simultence);

	musicexpr_free(me_workcopy);

	return midi_es;
}

static int
compare_midievents(const void *a, const void *b)
{
	const struct midievent *ma, *mb;

	ma = a;
	mb = b;

	return (ma->time_as_measures < mb->time_as_measures)          ? -1 : 
	       (ma->time_as_measures > mb->time_as_measures)          ?  1 :
	       (ma->eventtype == NOTEOFF && mb->eventtype == NOTEON)  ? -1 :
	       (ma->eventtype == NOTEON  && mb->eventtype == NOTEOFF) ?  1 :
	       0;
}

static struct mdl_stream *
offsetexprstream_to_midievents(struct mdl_stream *offset_es, int level)
{
	struct mdl_stream *midi_es;
	struct midievent *midievent;
	struct offsetexpr_t offsetexpr;
	struct musicexpr_t *me;
	float timeoffset;
	int i, ret;

	mdl_log(2, level, "offset expression stream to midi events\n");

	if ((midi_es = midi_eventstream_new()) == NULL)
		goto error;

	for (i = 0; i < offset_es->count; i++) {
		offsetexpr = offset_es->mexprs[i];
		me = offsetexpr.me;
		timeoffset = offsetexpr.offset;

		mdl_log(4,
			level + 1,
			"handling expression with offset %.3f\n",
			timeoffset);
		musicexpr_log(me, 4, level + 2, NULL);

		ret = add_musicexpr_to_midievents(midi_es,
						  me,
						  timeoffset,
						  level + 1);
		if (ret != 0)
			goto error;
	}

	ret = heapsort(midi_es->midievents,
		       midi_es->count,
		       sizeof(struct midievent),
		       compare_midievents);
	if (ret == -1) {
		warn("could not sort midieventstream");
		goto error;
	}

	/* add SONG_END midievent */
	midievent = &midi_es->midievents[ midi_es->count ];
	bzero(midievent, sizeof(struct midievent));
	midievent->eventtype = SONG_END;

	ret = mdl_stream_increment(midi_es);
	if (ret != 0)
		goto error;

	return midi_es;

error:
	warnx("could not convert offset-expression-stream to midi stream");
	if (midi_es)
		mdl_stream_free(midi_es);

	return NULL;
}

static int
add_musicexpr_to_midievents(struct mdl_stream *midi_es,
			    const struct musicexpr_t *me,
			    float timeoffset,
			    int level)
{
	struct midievent *midievent;
	struct musicexpr_t *noteoffsetexpr, *subexpr;
	int ret, new_note, i;

	ret = 0;

	switch (me->me_type) {
	case ME_TYPE_ABSNOTE:
		new_note = me->u.absnote.note;

		/* we accept and ignore notes that are out-of-range */
		if (new_note < 0 || MIDI_NOTE_MAX < new_note) {
			mdl_log(2,
				level,
				"skipping note with value %d",
				new_note);
			ret = 0;
			break;
		}
		/* length can never be non-positive here, that is a bug */
		assert(me->u.absnote.length > 0);

		midievent = &midi_es->midievents[ midi_es->count ];
		bzero(midievent, sizeof(struct midievent));
		midievent->eventtype = NOTEON;
		midievent->channel = DEFAULT_MIDICHANNEL;
		midievent->note = new_note;
		midievent->time_as_measures = timeoffset;
		midievent->velocity = DEFAULT_VELOCITY;

		ret = mdl_stream_increment(midi_es);
		if (ret != 0)
			break;

		midievent = &midi_es->midievents[ midi_es->count ];
		bzero(midievent, sizeof(struct midievent));
		midievent->eventtype = NOTEOFF;
		midievent->channel = DEFAULT_MIDICHANNEL;
		midievent->note = new_note;
		midievent->time_as_measures
		    = timeoffset + me->u.absnote.length;
		midievent->velocity = 0;

		ret = mdl_stream_increment(midi_es);
		break;
	case ME_TYPE_CHORD:
		noteoffsetexpr = chord_to_noteoffsetexpr(me->u.chord, level);
		ret = add_musicexpr_to_midievents(midi_es,
						  noteoffsetexpr,
						  timeoffset,
					    	  level);
		musicexpr_free(noteoffsetexpr);
		break;
	case ME_TYPE_NOTEOFFSETEXPR:
		for (i = 0; i < me->u.noteoffsetexpr.count; i++) {
			subexpr = musicexpr_clone(me->u.noteoffsetexpr.me,
						  level);
			apply_noteoffset(subexpr,
					 me->u.noteoffsetexpr.offsets[i],
					 level);

			ret = add_musicexpr_to_midievents(midi_es,
							  subexpr,
							  timeoffset,
							  level);
			musicexpr_free(subexpr);
			if (ret != 0)
				break;
		}
		break;
	default:
		assert(0);
		break;
	}

	return ret;
}

static struct musicexpr_t *
musicexpr_tq(enum musicexpr_type me_type,
	     int level,
	     struct musicexpr_t *listitem,
	     va_list va)
{
	struct musicexpr_t *me;

	if ((me = malloc(sizeof(struct musicexpr_t))) == NULL) {
		warnx("malloc in musicexpr_tq");
		return NULL;
	}

	assert(me_type == ME_TYPE_SEQUENCE || me_type == ME_TYPE_SIMULTENCE);

	me->me_type = me_type;

	TAILQ_INIT(&me->u.melist);

	while (listitem != NULL) {
		TAILQ_INSERT_TAIL(&me->u.melist, listitem, tq);
		listitem = va_arg(va, struct musicexpr_t *);
	}

	va_end(va);

	return me;
}

struct musicexpr_t *
musicexpr_sequence(int level, struct musicexpr_t *next_me, ...)
{
	va_list va;
	struct musicexpr_t *me;

	va_start(va, next_me);
	me = musicexpr_tq(ME_TYPE_SEQUENCE, level, next_me, va);
	va_end(va);

	return me;
}

struct musicexpr_t *
musicexpr_simultence(int level, struct musicexpr_t *next_me, ...)
{
	va_list va;
	struct musicexpr_t *me;

	va_start(va, next_me);
	me = musicexpr_tq(ME_TYPE_SIMULTENCE, level, next_me, va);
	va_end(va);

	return me;
}

static int
add_musicexpr_to_flat_simultence(struct musicexpr_t *simultence,
				 struct musicexpr_t *me,
				 struct simultence_state *state,
				 int level)
{
	struct musicexpr_t *cloned, *noteoffsetexpr, *offsetexpr, *p,
			   *scaled_me, *subexpr;
	float end_offset, new_next_offset, old_offset;
	int noteoffset, ret, i;

	assert(me->me_type != ME_TYPE_JOINEXPR);
	assert(me->me_type != ME_TYPE_RELNOTE);

	new_next_offset = old_offset = state->next_offset;

	mdl_log(3,
		level,
		"handling musicexpr %p/%s in simultence conversion\n",
		me,
		musicexpr_type_to_string(me));

	switch (me->me_type) {
        case ME_TYPE_ABSNOTE:
		if ((cloned = musicexpr_clone(me, level + 1)) == NULL)
			return 1;
		offsetexpr = malloc(sizeof(struct musicexpr_t));
		if (offsetexpr == NULL) {
			warn("malloc in add_musicexpr_to_flat_simultence");
			musicexpr_free(cloned);
		}
		offsetexpr->me_type = ME_TYPE_WITHOFFSET;
		offsetexpr->u.offsetexpr.me = cloned;
		offsetexpr->u.offsetexpr.offset = state->next_offset;

		mdl_log(3,
			level + 1,
			"adding expression %p to simultence %p\n",
			offsetexpr,
			simultence);
		musicexpr_log(offsetexpr, 3, level + 2, NULL);
		TAILQ_INSERT_TAIL(&simultence->u.melist, offsetexpr, tq);

		end_offset = state->next_offset + cloned->u.absnote.length;
		state->length = MAX(end_offset, state->length);
		state->length_no_rests = MAX(end_offset,
					     state->length_no_rests);
		state->next_offset = end_offset;
		break;
        case ME_TYPE_CHORD:
		noteoffsetexpr = chord_to_noteoffsetexpr(me->u.chord, level);
		if (noteoffsetexpr == NULL)
			return 1;
		ret = add_musicexpr_to_flat_simultence(simultence,
						       noteoffsetexpr,
						       state,
						       level + 1);
		musicexpr_free(noteoffsetexpr);
		if (ret != 0)
			return ret;
		break;
        case ME_TYPE_EMPTY:
		/* nothing to do */
		break;
        case ME_TYPE_NOTEOFFSETEXPR:
		for (i = 0; i < me->u.noteoffsetexpr.count; i++) {
			subexpr = musicexpr_clone(me->u.noteoffsetexpr.me,
						  level);
			if (subexpr == NULL)
				return 1;
			noteoffset = me->u.noteoffsetexpr.offsets[i];
			apply_noteoffset(subexpr, noteoffset, level);
			old_offset = state->next_offset;
			ret = add_musicexpr_to_flat_simultence(simultence,
							       subexpr,
							       state,
							       level + 1);
			if (ret != 0)
				return ret;
			new_next_offset = MAX(state->next_offset,
					      new_next_offset);
			state->next_offset = old_offset;
		}
		state->next_offset = new_next_offset;
		break;
        case ME_TYPE_REST:
		end_offset = state->next_offset + me->u.rest.length;
		state->length = MAX(end_offset, state->length);
		state->next_offset = end_offset;
		break;
        case ME_TYPE_SCALEDEXPR:
		scaled_me = musicexpr_scale_in_time(me->u.scaledexpr.me,
						    me->u.scaledexpr.length,
						    level + 1);
		if (scaled_me == NULL)
			return 1;
		ret = add_musicexpr_to_flat_simultence(simultence,
						       scaled_me,
						       state,
						       level + 1);
		musicexpr_free(scaled_me);
		if (ret != 0)
			return ret;
		break;
        case ME_TYPE_SEQUENCE:
		TAILQ_FOREACH(p, &me->u.melist, tq) {
			ret = add_musicexpr_to_flat_simultence(simultence,
							       p,
							       state,
							       level + 1);
			if (ret != 0)
				return ret;
		}
		break;
        case ME_TYPE_SIMULTENCE:
		TAILQ_FOREACH(p, &me->u.melist, tq) {
			old_offset = state->next_offset;
			ret = add_musicexpr_to_flat_simultence(simultence,
							       p,
							       state,
							       level + 1);
			if (ret != 0)
				return ret;
			new_next_offset = MAX(state->next_offset,
					      new_next_offset);
			state->next_offset = old_offset;
		}
		state->next_offset = new_next_offset;
		break;
        case ME_TYPE_WITHOFFSET:
		state->next_offset += me->u.offsetexpr.offset;
		ret = add_musicexpr_to_flat_simultence(simultence,
						       me->u.offsetexpr.me,
						       state,
						       level + 1);
		if (ret != 0)
			return ret;
		break;
	default:
		assert(0);
	}

	mdl_log(3,
		level,
		"offset changed from %f to %f\n",
		old_offset,
		state->next_offset);

	return 0;
}

struct musicexpr_t *
musicexpr_to_flat_simultence(struct musicexpr_t *me, int level)
{
	struct musicexpr_t *rest, *simultence;
	struct simultence_state state;
	int ret;

	state.length          = 0;
	state.length_no_rests = 0;
	state.next_offset     = 0;
	
	if ((simultence = musicexpr_simultence(level, NULL)) == NULL)
		return NULL;

	ret = add_musicexpr_to_flat_simultence(simultence, me, &state, level);
	if (ret != 0) {
		warnx("failed to add a musicexpr to simultence");
		musicexpr_free(simultence);
		return NULL;
	}

	if (state.length > state.length_no_rests) {
		if ((rest = malloc(sizeof(struct musicexpr_t))) == NULL) {
			warn("malloc in musicexpr_to_flat_simultence");
			musicexpr_free(simultence);
			return NULL;
		}

		rest->me_type = ME_TYPE_REST;
		rest->u.rest.length = state.length - state.length_no_rests;
		state.next_offset = state.length_no_rests;
		ret = add_musicexpr_to_flat_simultence(simultence,
						       rest,
						       &state,
						       level);
		if (ret != 0) {
			musicexpr_free(simultence);
			musicexpr_free(rest);
			return NULL;
		}
	}

	return simultence;
}

static void
apply_noteoffset(struct musicexpr_t *me, int offset, int level)
{
	struct musicexpr_t *p;

	/* XXX there is probably a common pattern here: do some operation
	 * XXX to all subexpressions... but the knowledge "what are the
	 * XXX subexpressions" is not anywhere */

	assert(me->me_type != ME_TYPE_RELNOTE);

	switch (me->me_type) {
        case ME_TYPE_ABSNOTE:
		me->u.absnote.note += offset;
		break;
        case ME_TYPE_CHORD:
		apply_noteoffset(me->u.chord.me, offset, level);
		break;
        case ME_TYPE_EMPTY:
		/* do nothing */
		break;
        case ME_TYPE_JOINEXPR:
		apply_noteoffset(me->u.joinexpr.a, offset, level);
		apply_noteoffset(me->u.joinexpr.b, offset, level);
		break;
        case ME_TYPE_NOTEOFFSETEXPR:
		apply_noteoffset(me->u.noteoffsetexpr.me, offset, level);
		break;
        case ME_TYPE_REST:
		/* do nothing */
		break;
        case ME_TYPE_SCALEDEXPR:
		apply_noteoffset(me->u.scaledexpr.me, offset, level);
		break;
        case ME_TYPE_SEQUENCE:
        case ME_TYPE_SIMULTENCE:
		TAILQ_FOREACH(p, &me->u.melist, tq)
			apply_noteoffset(p, offset, level);
		break;
        case ME_TYPE_WITHOFFSET:
		apply_noteoffset(me->u.offsetexpr.me, offset, level);
		break;
	default:
		assert(0);
	}
}

static struct musicexpr_t *
musicexpr_scale_in_time(struct musicexpr_t *me,
			float target_length,
			int level)
{
	struct musicexpr_t *new_me;
	float me_length;

	assert(target_length > 0);

	mdl_log(3,
		level,
		"scaling musicexpr %p/%s to target length %.3f\n",
		me,
		musicexpr_type_to_string(me),
		target_length);

	if (target_length < MINIMUM_MUSICEXPR_LENGTH) {
		mdl_log(3, level + 1,
			"target length %.3f is too short,"
			  " returning an empty expression\n",
			me,
			musicexpr_type_to_string(me),
			target_length);
		return musicexpr_new_empty();
	}

	me_length = musicexpr_calc_length(me);
	assert(me_length >= MINIMUM_MUSICEXPR_LENGTH);

	if ((new_me = musicexpr_clone(me, level + 1)) == NULL)
		return NULL;

	musicexpr_stretch_length_by_factor(new_me,
					   target_length / me_length);

	return new_me;
}

void
musicexpr_stretch_length_by_factor(struct musicexpr_t *me,
				   float factor)
{
	struct musicexpr_t *p;

	assert(me->me_type != ME_TYPE_RELNOTE);

	switch (me->me_type) {
        case ME_TYPE_ABSNOTE:
		me->u.absnote.length *= factor;
		break;
        case ME_TYPE_CHORD:
		musicexpr_stretch_length_by_factor(me->u.chord.me, factor);
		break;
        case ME_TYPE_EMPTY:
		break;
        case ME_TYPE_JOINEXPR:
		musicexpr_stretch_length_by_factor(me->u.joinexpr.a, factor);
		musicexpr_stretch_length_by_factor(me->u.joinexpr.b, factor);
		break;
        case ME_TYPE_NOTEOFFSETEXPR:
		musicexpr_stretch_length_by_factor(me->u.noteoffsetexpr.me,
						   factor);
		break;
        case ME_TYPE_REST:
		me->u.rest.length *= factor;
		break;
        case ME_TYPE_SCALEDEXPR:
		me->u.scaledexpr.length *= factor;
		break;
        case ME_TYPE_SEQUENCE:
        case ME_TYPE_SIMULTENCE:
		TAILQ_FOREACH(p, &me->u.melist, tq)
			musicexpr_stretch_length_by_factor(p, factor);
		break;
        case ME_TYPE_WITHOFFSET:
		musicexpr_stretch_length_by_factor(me->u.offsetexpr.me,
						   factor);
		break;
	default:
		assert(0);
	}
}

static float
musicexpr_calc_length(struct musicexpr_t *me)
{
	struct musicexpr_t *p;
	float length;

	assert(me->me_type != ME_TYPE_RELNOTE);

	length = 0.0;

	switch (me->me_type) {
        case ME_TYPE_ABSNOTE:
		length = me->u.absnote.length;
		break;
        case ME_TYPE_CHORD:
		length = musicexpr_calc_length(me->u.chord.me);
		break;
        case ME_TYPE_EMPTY:
		break;
        case ME_TYPE_JOINEXPR:
		length = musicexpr_calc_length(me->u.joinexpr.a)
			   + musicexpr_calc_length(me->u.joinexpr.b);
		break;
        case ME_TYPE_NOTEOFFSETEXPR:
		length = musicexpr_calc_length(me->u.noteoffsetexpr.me);
		break;
        case ME_TYPE_REST:
		length = me->u.rest.length;
		break;
        case ME_TYPE_SCALEDEXPR:
		length = me->u.scaledexpr.length;
		break;
        case ME_TYPE_SEQUENCE:
		TAILQ_FOREACH(p, &me->u.melist, tq)
			length += musicexpr_calc_length(p);
		break;
        case ME_TYPE_SIMULTENCE:
		TAILQ_FOREACH(p, &me->u.melist, tq)
			length = MAX(length, musicexpr_calc_length(p));
		break;
        case ME_TYPE_WITHOFFSET:
		length = me->u.offsetexpr.offset
			   + musicexpr_calc_length(me->u.offsetexpr.me);
		break;
	default:
		assert(0);
	}

	return length;
}

void
musicexpr_log(const struct musicexpr_t *me,
	      int loglevel,
	      int indentlevel,
	      char *prefix)
{
	const char *metype_string;
	char *old_tmpstring, *tmpstring;
	char default_prefix[] = "";
	int ret, i;

	metype_string = musicexpr_type_to_string(me);

	if (prefix == NULL)
		prefix = default_prefix;

	switch (me->me_type) {
	case ME_TYPE_ABSNOTE:
		mdl_log(loglevel,
			indentlevel,
			"%s%s notesym=%d note=%d length=%.3f\n",
			prefix,
			metype_string,
			me->u.absnote.notesym,
			me->u.absnote.note,
			me->u.absnote.length);
		break;
	case ME_TYPE_CHORD:
		mdl_log(loglevel,
			indentlevel,
			"%s%s%s\n",
			prefix,
			metype_string);
		musicexpr_log_chordtype(me->u.chord.chordtype,
					loglevel,
					indentlevel + 1,
					prefix);
		musicexpr_log(me->u.chord.me,
			      loglevel,
			      indentlevel + 1,
			      prefix);
		break;
	case ME_TYPE_EMPTY:
		mdl_log(loglevel,
			indentlevel,
			"%s%s\n",
			prefix,
			metype_string);
		break;
	case ME_TYPE_JOINEXPR:
		mdl_log(loglevel,
			indentlevel,
			"%s%s\n",
			prefix,
			metype_string);
		musicexpr_log(me->u.joinexpr.a,
			      loglevel,
			      indentlevel + 1,
			      prefix);
		musicexpr_log(me->u.joinexpr.b,
			      loglevel,
			      indentlevel + 1,
			      prefix);
		break;
	case ME_TYPE_NOTEOFFSETEXPR:
		mdl_log(loglevel,
			indentlevel,
			"%s%s\n",
			prefix,
			metype_string);
		musicexpr_log(me->u.noteoffsetexpr.me,
			      loglevel,
			      indentlevel + 1,
			      prefix);
		if ((old_tmpstring = strdup("noteoffsets:")) == NULL)
			return;
		for (i = 0; i < me->u.noteoffsetexpr.count; i++) {
 			ret = asprintf(&tmpstring,
				       "%s %d",
				       old_tmpstring,
				       me->u.noteoffsetexpr.offsets[i]);
			free(old_tmpstring);
			if (ret == -1) {
				old_tmpstring = NULL;
				break;
			}
			old_tmpstring = tmpstring;
		}
		if (old_tmpstring != NULL) {
			mdl_log(loglevel,
				indentlevel + 1,
				"%s%s\n",
				prefix,
				old_tmpstring);
			free(old_tmpstring);
		}
		break;
	case ME_TYPE_RELNOTE:
		mdl_log(loglevel,
			indentlevel,
			"%s%s notesym=%d notemods=%d length=%.3f" \
			  " octavemods=%d\n",
			prefix,
			metype_string,
			me->u.relnote.notesym,
			me->u.relnote.notemods,
			me->u.relnote.length,
			me->u.relnote.octavemods);
		break;
	case ME_TYPE_RELSIMULTENCE:
		assert(me->u.scaledexpr.me->me_type == ME_TYPE_SIMULTENCE);
		mdl_log(loglevel,
			indentlevel,
			"%s%s length=%.3f\n",
			prefix,
			metype_string,
			me->u.scaledexpr.length);
		musicexpr_log(me->u.scaledexpr.me,
			      loglevel,
			      indentlevel + 1,
			      prefix);
		break;
	case ME_TYPE_REST:
		mdl_log(loglevel,
			indentlevel,
			"%s%s length=%.3f\n",
			prefix,
			metype_string,
			me->u.rest.length);
		break;
	case ME_TYPE_SCALEDEXPR:
		mdl_log(loglevel,
			indentlevel,
			"%s%s length=%.3f\n",
			prefix,
			metype_string,
			me->u.scaledexpr.length);
		musicexpr_log(me->u.scaledexpr.me,
			      loglevel,
			      indentlevel + 1,
			      prefix);
		break;
	case ME_TYPE_SEQUENCE:
		mdl_log(loglevel,
			indentlevel,
			"%s%s\n",
			prefix,
			metype_string);
		musicexpr_log_melist(me->u.melist,
				     loglevel,
				     indentlevel,
				     prefix);
		break;
	case ME_TYPE_SIMULTENCE:
		mdl_log(loglevel,
			indentlevel,
			"%s%s\n",
			prefix,
			metype_string);
		musicexpr_log_melist(me->u.melist,
				     loglevel,
				     indentlevel,
				     prefix);
		break;
	case ME_TYPE_WITHOFFSET:
		mdl_log(loglevel,
			indentlevel,
			"%s%s offset=%.3f\n",
			prefix,
			metype_string,
			me->u.offsetexpr.offset);
		musicexpr_log(me->u.offsetexpr.me,
			      loglevel,
			      indentlevel + 1,
			      prefix);
		break;
	default:
		assert(0);
	}
}

static void
musicexpr_log_chordtype(enum chordtype_t chordtype,
			int loglevel,
			int indentlevel,
			char *prefix)
{
	const char *chordnames[] = {
		"none",		/* CHORDTYPE_NONE     */
		"5",		/* CHORDTYPE_MAJ      */
		"m",		/* CHORDTYPE_MIN      */
		"aug",		/* CHORDTYPE_AUG      */
		"dim",		/* CHORDTYPE_DIM      */
		"7",		/* CHORDTYPE_7        */
		"maj7",		/* CHORDTYPE_MAJ7     */
		"m7",		/* CHORDTYPE_MIN7     */
		"dim7",		/* CHORDTYPE_DIM7     */
		"aug7",		/* CHORDTYPE_AUG7     */
		"m7.5-",	/* CHORDTYPE_DIM5MIN7 */
		"m7+",		/* CHORDTYPE_MIN5MAJ7 */
		"6",		/* CHORDTYPE_MAJ6     */
		"m6",		/* CHORDTYPE_MIN6     */
		"9",		/* CHORDTYPE_9        */
		"maj9",		/* CHORDTYPE_MAJ9     */
		"m9",		/* CHORDTYPE_MIN9     */
		"11",		/* CHORDTYPE_11       */
		"maj11",	/* CHORDTYPE_MAJ11    */
		"m11",		/* CHORDTYPE_MIN11    */
		"13",		/* CHORDTYPE_13       */
		"13.11",	/* CHORDTYPE_13_11    */
		"maj13.11",	/* CHORDTYPE_MAJ13_11 */
		"m13.11",	/* CHORDTYPE_MIN13_11 */
		"sus2",		/* CHORDTYPE_SUS2     */
		"sus4",		/* CHORDTYPE_SUS4     */
		"1.5",		/* CHORDTYPE_5        */
		"1.5.8",	/* CHORDTYPE_5_8      */
	};

	assert(0 <= chordtype && chordtype < CHORDTYPE_MAX);

	mdl_log(loglevel,
		indentlevel,
		"%schordtype %s\n",
		prefix,
		chordnames[chordtype]);
}

static void
musicexpr_log_melist(struct melist_t melist,
		     int loglevel,
		     int indentlevel,
		     char *prefix)
{
	struct musicexpr_t *p;

	TAILQ_FOREACH(p, &melist, tq)
		musicexpr_log(p, loglevel, indentlevel + 1, prefix);
}

void
musicexpr_free(struct musicexpr_t *me)
{
	switch (me->me_type) {
	case ME_TYPE_ABSNOTE:
	case ME_TYPE_EMPTY:
	case ME_TYPE_RELNOTE:
	case ME_TYPE_REST:
		break;
	case ME_TYPE_CHORD:
		musicexpr_free(me->u.chord.me);
		break;
	case ME_TYPE_JOINEXPR:
		musicexpr_free(me->u.joinexpr.a);
		musicexpr_free(me->u.joinexpr.b);
		break;
	case ME_TYPE_NOTEOFFSETEXPR:
		musicexpr_free(me->u.noteoffsetexpr.me);
		break;
	case ME_TYPE_RELSIMULTENCE:
	case ME_TYPE_SCALEDEXPR:
		musicexpr_free(me->u.scaledexpr.me);
		break;
	case ME_TYPE_SEQUENCE:
	case ME_TYPE_SIMULTENCE:
		musicexpr_free_melist(me->u.melist);
		break;
	case ME_TYPE_WITHOFFSET:
		musicexpr_free(me->u.offsetexpr.me);
		break;
	default:
		assert(0);
	}

	free(me);
}

void
musicexpr_free_melist(struct melist_t melist)
{
	struct musicexpr_t *p, *q;

	TAILQ_FOREACH_SAFE(p, &melist, tq, q) {
		TAILQ_REMOVE(&melist, p, tq);
		musicexpr_free(p);
	}
}

static struct mdl_stream *
offsetexprstream_new(void)
{
	return mdl_stream_new(OFFSETEXPRSTREAM);
}

struct musicexpr_t *
chord_to_noteoffsetexpr(struct chord_t chord, int level)
{
	static const struct {
		size_t count;
		int offsets[7];
	} chord_noteoffsets[] = {
		{ 0                              }, /* CHORDTYPE_NONE     */
		{ 3, { 0, 4, 7                 } }, /* CHORDTYPE_MAJ      */
		{ 3, { 0, 3, 7                 } }, /* CHORDTYPE_MIN      */
		{ 3, { 0, 4, 8                 } }, /* CHORDTYPE_AUG      */
		{ 3, { 0, 3, 6                 } }, /* CHORDTYPE_DIM      */
		{ 4, { 0, 4, 7, 10             } }, /* CHORDTYPE_7        */
		{ 4, { 0, 4, 7, 11             } }, /* CHORDTYPE_MAJ7     */
		{ 4, { 0, 3, 7, 10             } }, /* CHORDTYPE_MIN7     */
		{ 4, { 0, 3, 6,  9             } }, /* CHORDTYPE_DIM7     */
		{ 4, { 0, 4, 8, 10             } }, /* CHORDTYPE_AUG7     */
		{ 4, { 0, 3, 5, 10             } }, /* CHORDTYPE_DIM5MIN7 */
		{ 4, { 0, 3, 7, 11             } }, /* CHORDTYPE_MIN5MAJ7 */
		{ 4, { 0, 4, 7,  9             } }, /* CHORDTYPE_MAJ6     */
		{ 4, { 0, 3, 7,  9             } }, /* CHORDTYPE_MIN6     */
		{ 5, { 0, 4, 7, 10, 14         } }, /* CHORDTYPE_9        */
		{ 5, { 0, 4, 7, 11, 14         } }, /* CHORDTYPE_MAJ9     */
		{ 5, { 0, 3, 7, 10, 14         } }, /* CHORDTYPE_MIN9     */
		{ 6, { 0, 4, 7, 10, 14, 17     } }, /* CHORDTYPE_11       */
		{ 6, { 0, 4, 7, 11, 14, 17     } }, /* CHORDTYPE_MAJ11    */
		{ 6, { 0, 3, 7, 10, 14, 17     } }, /* CHORDTYPE_MIN11    */
		{ 6, { 0, 4, 7, 10, 14,     21 } }, /* CHORDTYPE_13       */
		{ 7, { 0, 4, 7, 10, 14, 17, 21 } }, /* CHORDTYPE_13_11    */
		{ 7, { 0, 4, 7, 11, 14, 17, 21 } }, /* CHORDTYPE_MAJ13_11 */
		{ 7, { 0, 3, 7, 10, 14, 17, 21 } }, /* CHORDTYPE_MIN13_11 */
		{ 3, { 0, 2, 7                 } }, /* CHORDTYPE_SUS2     */
		{ 3, { 0, 5, 7                 } }, /* CHORDTYPE_SUS4     */
		{ 2, { 0,    7                 } }, /* CHORDTYPE_5        */
		{ 2, { 0, 7, 12                } }, /* CHORDTYPE_5_8      */
	};

	struct musicexpr_t *me;
	enum chordtype_t chordtype;

	if ((me = malloc(sizeof(struct musicexpr_t))) == NULL) {
		warn("malloc in chord_to_noteoffsetexpr");
		return NULL;
	}

	chordtype = chord.chordtype;

	assert(chord.me->me_type == ME_TYPE_ABSNOTE);
	assert(0 <= chordtype && chordtype < CHORDTYPE_MAX);

	me->me_type                  = ME_TYPE_NOTEOFFSETEXPR;
	me->u.noteoffsetexpr.me      = musicexpr_clone(chord.me, level);
	me->u.noteoffsetexpr.count   = chord_noteoffsets[chordtype].count;
	me->u.noteoffsetexpr.offsets = chord_noteoffsets[chordtype].offsets;

	return me;
}

void
free_melist(struct musicexpr_t *me)
{
	struct musicexpr_t *p, *q;

	assert(me->me_type == ME_TYPE_SEQUENCE
		 || me->me_type == ME_TYPE_SIMULTENCE);

	TAILQ_FOREACH_SAFE(p, &me->u.melist, tq, q)
		TAILQ_REMOVE(&me->u.melist, p, tq);
}

const char *
musicexpr_type_to_string(const struct musicexpr_t *me)
{
	static const char *strings[] = {
		"absnote",		/* ME_TYPE_ABSNOTE */
		"chord",		/* ME_TYPE_CHORD */
		"empty",		/* ME_TYPE_EMPTY */
		"joinexpr",		/* ME_TYPE_JOINEXPR */
		"noteoffsetexpr",	/* ME_TYPE_NOTEOFFSETEXPR */
		"relnote",		/* ME_TYPE_RELNOTE */
		"relsimultence",	/* ME_TYPE_RELSIMULTENCE */
		"rest",			/* ME_TYPE_REST */
		"scaledexpr",		/* ME_TYPE_SCALEDEXPR */
		"sequence",		/* ME_TYPE_SEQUENCE */
		"simultence",		/* ME_TYPE_SIMULTENCE */
		"withoffset",		/* ME_TYPE_WITHOFFSET */
	};

	assert(ME_TYPE_ABSNOTE <= me->me_type && me->me_type < ME_TYPE_COUNT);

	return strings[me->me_type];
}

void
musicexpr_copy(struct musicexpr_t *dst, struct musicexpr_t *src)
{
	dst->me_type = src->me_type;
	dst->u = src->u;
}

static struct musicexpr_t *
musicexpr_new_empty(void)
{
	struct musicexpr_t *me;

	if ((me = malloc(sizeof(struct musicexpr_t))) == NULL) {
		warn("malloc in musicexpr_new_empty");
		return NULL;
	}

	me->me_type = ME_TYPE_EMPTY;

	return me;
}
