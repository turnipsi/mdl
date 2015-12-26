/* $Id: musicexpr.c,v 1.45 2015/12/26 20:33:31 je Exp $ */

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

static struct musicexpr_t	*musicexpr_clone(struct musicexpr_t *, int);
static int			 musicexpr_clone_melist(struct melist_t *,
			 				struct melist_t,
							int);

static struct mdl_stream *offsetexprstream_new(void);

static void	musicexpr_relative_to_absolute(struct musicexpr_t *, int);

static void	relative_to_absolute(struct musicexpr_t *,
				     struct previous_relative_exprs_t *,
				     int);
static int	musicexpr_flatten(struct mdl_stream *, struct musicexpr_t *);
static int	offset_expressions(struct mdl_stream *,
				   struct musicexpr_t *,
				   float *,
				   int);

static int	add_new_offset_expression(struct mdl_stream *,
					  struct musicexpr_t *,
					  float offset);

static struct tqitem_me *musicexpr_clone_to_tqitem(struct musicexpr_t *, int);

static float	musicexpr_length(struct musicexpr_t *);
static void	musicexpr_log_chordtype(enum chordtype_t, int, int);
static void	musicexpr_log_melist(struct melist_t, int, int);

static struct musicexpr_t *
musicexpr_tq(enum musicexpr_type me_type, va_list va);

static struct mdl_stream *
offsetexprstream_to_midievents(struct mdl_stream *, int);

static void	apply_noteoffset(struct musicexpr_t *, int, int);

static int
add_musicexpr_to_midievents(struct mdl_stream *,
			    const struct musicexpr_t *,
			    float,
			    int);

static int	compare_notesyms(enum notesym_t, enum notesym_t);
static int	compare_midievents(const void *, const void *);

struct {
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


static int
musicexpr_flatten(struct mdl_stream *oes, struct musicexpr_t *me)
{
	float offset;

	offset = 0.0;

	mdl_log(1, 1, "flattening music expression\n");
	mdl_log(2, 2, "initializing offset to %.3f\n", offset);

	if (offset_expressions(oes, me, &offset, 2) != 0)
		return 1;

	return 0;
}

static int
offset_expressions(struct mdl_stream *oes,
		   struct musicexpr_t *me,
		   float *offset,
		   int level)
{
	struct tqitem_me *p;
	float old_offset;
	int ret;

	assert(me->me_type != ME_TYPE_RELNOTE
		 && me->me_type != ME_TYPE_JOINEXPR);

	old_offset = *offset;

	switch (me->me_type) {
	case ME_TYPE_ABSNOTE:
		mdl_log(3, level, "finding offset expression for absnote\n");
		musicexpr_log(me, 4, level + 1);
		if ((ret = add_new_offset_expression(oes, me, *offset)) != 0)
			return ret;
		*offset += musicexpr_length(me);
		break;
	case ME_TYPE_REST:
		mdl_log(3, level, "finding offset expression for rest\n");
		musicexpr_log(me, 4, level + 1);
		*offset += musicexpr_length(me);
		break;
	case ME_TYPE_SEQUENCE:
		mdl_log(3, level, "finding offset expression for sequence\n");
		musicexpr_log(me, 4, level + 1);
		TAILQ_FOREACH(p, &me->melist, tq) {
			ret = offset_expressions(oes,
						 p->me,
						 offset,
						 level + 1);
			if (ret != 0)
				return ret;
		}
		break;
	case ME_TYPE_WITHOFFSET:
		mdl_log(3, level, "finding offset expression for" \
				    " offset expression\n");
		musicexpr_log(me, 4, level + 1);
		*offset += me->offsetexpr.offset;
		ret = offset_expressions(oes,
					 me->offsetexpr.me,
					 offset,
					 level + 1);
		if (ret != 0)
			return ret;
		break;
	case ME_TYPE_CHORD:
		mdl_log(3, level, "finding offset expression for chord\n");
		musicexpr_log(me, 4, level + 1);
		if ((ret = add_new_offset_expression(oes, me, *offset)) != 0)
			return ret;
		*offset += musicexpr_length(me->chord.me);
		break;
	default:
		assert(0);
	}

	if (old_offset != *offset) {
		mdl_log(3,
			level,
			"offset changed from %.3f to %.3f\n",
			old_offset,
			*offset);
	}

	return 0;
}

static int
add_new_offset_expression(struct mdl_stream *oes,
			  struct musicexpr_t *me,
			  float offset)
{			
	assert(oes->s_type == OFFSETEXPRSTREAM);

	oes->mexprs[ oes->count ].me = me;
	oes->mexprs[ oes->count ].offset = offset;

	return mdl_stream_increment(oes);
}

static struct musicexpr_t *
musicexpr_clone(struct musicexpr_t *me, int level)
{
	struct musicexpr_t *cloned;
	int ret;

	cloned = malloc(sizeof(struct musicexpr_t));
	if (cloned == NULL) {
		warn("malloc failure when cloning musicexpr");
		return NULL;
	}

	*cloned = *me;

	switch (me->me_type) {
	case ME_TYPE_ABSNOTE:
		mdl_log(4, level, "cloning expression %p (absnote)\n", me);
		musicexpr_log(me, 4, level + 1);
		break;
	case ME_TYPE_RELNOTE:
		mdl_log(4, level, "cloning expression %p (relnote)\n", me);
		musicexpr_log(me, 4, level + 1);
		break;
	case ME_TYPE_REST:
		mdl_log(4, level, "cloning expression %p (rest)\n", me);
		musicexpr_log(me, 4, level + 1);
		break;
	case ME_TYPE_JOINEXPR:
		mdl_log(4, level, "cloning expression %p (joinexpr)\n", me);
		cloned->joinexpr.a = musicexpr_clone(me->joinexpr.a,
						     level + 1);
		if (cloned->joinexpr.a == NULL) {
			free(cloned);
			cloned = NULL;
			break;
		}
		cloned->joinexpr.b = musicexpr_clone(me->joinexpr.b,
						     level + 1);
		if (cloned->joinexpr.b == NULL) {
			musicexpr_free(cloned->joinexpr.a);
			free(cloned);
			cloned = NULL;
		}
		break;
	case ME_TYPE_SEQUENCE:
		mdl_log(4, level, "cloning expression %p (sequence)\n", me);
		ret = musicexpr_clone_melist(&cloned->melist,
					     me->melist,
					     level + 1);
		if (ret != 0) {
			free(cloned);
			cloned = NULL;
		}
		break;
	case ME_TYPE_WITHOFFSET:
		mdl_log(4, level, "cloning expression %p (withoffset)\n", me);
		musicexpr_log(me, 4, level + 1);
		cloned->offsetexpr.me = musicexpr_clone(me->offsetexpr.me,
							 level + 1);
		if (cloned->offsetexpr.me == NULL) {
			free(cloned);
			cloned = NULL;
		}
		break;
	case ME_TYPE_CHORD:
		mdl_log(4, level, "cloning expression %p (chord)\n", me);
		musicexpr_log(me, 4, level + 1);
		cloned->chord.me = musicexpr_clone(me->chord.me, level + 1);
		if (cloned->chord.me == NULL) {
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
	struct tqitem_me *p, *q;

	TAILQ_INIT(cloned_melist);

	TAILQ_FOREACH(p, &melist, tq) {
		q = musicexpr_clone_to_tqitem(p->me, level);
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

static float
musicexpr_length(struct musicexpr_t *me)
{
	float length;

	switch (me->me_type) {
	case ME_TYPE_ABSNOTE:
		length = me->absnote.length;
		break;
	case ME_TYPE_RELNOTE:
		length = me->relnote.length;
		break;
	case ME_TYPE_REST:
		length = me->rest.length;
		break;
	default:
		assert(0);
	}

	return length;
}

static void
relative_to_absolute(struct musicexpr_t *me,
		     struct previous_relative_exprs_t *prev_exprs,
		     int level)
{
	struct tqitem_me *p;
	struct absnote_t absnote;
	struct relnote_t relnote;
	int notevalues[] = {
		/* for NOTE_C, NOTE_D, ... */
		0, 2, 4, 5, 7, 9, 11,
	};
	int note, c;

	switch (me->me_type) {
	case ME_TYPE_ABSNOTE:
		mdl_log(3, level, "rel->abs expression (absnote)\n");
		/* pass as is, but this affects previous absnote */
		prev_exprs->absnote = me->absnote;
		break;
	case ME_TYPE_RELNOTE:
		mdl_log(3, level, "rel->abs expression (relnote)\n");
		musicexpr_log(me, 3, level + 1);
		relnote = me->relnote;

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
		me->absnote = absnote;

		prev_exprs->absnote = absnote;

		break;
	case ME_TYPE_REST:
		mdl_log(3, level, "rel->abs expression (rest)\n");
		musicexpr_log(me, 3, level + 1);

		if (me->rest.length == 0) {
			me->rest.length = prev_exprs->absnote.length;
		} else {
			prev_exprs->absnote.length = me->rest.length;
		}
		break;
	case ME_TYPE_JOINEXPR:
		mdl_log(3, level, "rel->abs expression (joinexpr)\n");
		relative_to_absolute(me->joinexpr.a, prev_exprs, level + 1);
		relative_to_absolute(me->joinexpr.b, prev_exprs, level + 1);
		break;
	case ME_TYPE_SEQUENCE:
		mdl_log(3, level, "rel->abs expression (sequence)\n");
		TAILQ_FOREACH(p, &me->melist, tq)
			relative_to_absolute(p->me, prev_exprs, level + 1);
		break;
	case ME_TYPE_WITHOFFSET:
		mdl_log(3, level, "rel->abs expression (withoffset)\n");
		relative_to_absolute(me->offsetexpr.me,
				     prev_exprs,
				     level + 1);
		break;
	case ME_TYPE_CHORD:
		mdl_log(3, level, "rel->abs expression (chord)\n");
		relative_to_absolute(me->chord.me, prev_exprs, level + 1);
		if (me->chord.chordtype == CHORDTYPE_NONE)
			me->chord.chordtype = prev_exprs->chordtype;
		prev_exprs->chordtype = me->chord.chordtype;
		break;
	default:
		assert(0);
		break;
	}

	if (me->me_type == ME_TYPE_ABSNOTE || me->me_type == ME_TYPE_REST)
		musicexpr_log(me, 3, level + 1);
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
	struct musicexpr_t *me_workcopy;
	struct mdl_stream *offset_es, *midi_es;

	mdl_log(1, level, "converting music expression to midi stream\n");

	midi_es = NULL;

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

	if (joinexpr_musicexpr(me_workcopy, level + 1) != 0) {
		warnx("error occurred in joining music expressions");
		goto finish;
	}

	if (musicexpr_flatten(offset_es, me_workcopy) != 0) {
		warnx("could not flatten music expression"
			" to offset-expression-stream");
		goto finish;
	}

	midi_es = offsetexprstream_to_midievents(offset_es, level + 1);
	if (midi_es == NULL)
		warnx("could not convert offset-expression-stream" \
			" to midistream");

finish:
	mdl_stream_free(offset_es);
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
	struct musicexpr_with_offset_t offsetexpr;
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
		musicexpr_log(me, 4, level + 2);

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
		new_note = me->absnote.note;

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
		assert(me->absnote.length > 0);

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
		midievent->time_as_measures = timeoffset + me->absnote.length;
		midievent->velocity = 0;

		ret = mdl_stream_increment(midi_es);
		break;
	case ME_TYPE_CHORD:
		noteoffsetexpr = chord_to_noteoffsetexpr(me->chord, level);
		ret = add_musicexpr_to_midievents(midi_es,
						  noteoffsetexpr,
						  timeoffset,
					    	  level);
		musicexpr_free(noteoffsetexpr);
		break;
	case ME_TYPE_NOTEOFFSETEXPR:
		for (i = 0; i < me->noteoffsetexpr.count; i++) {
			subexpr = musicexpr_clone(me->noteoffsetexpr.me,
						  level);
			apply_noteoffset(subexpr,
					 me->noteoffsetexpr.offsets[i],
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
musicexpr_tq(enum musicexpr_type me_type, va_list va)
{
	struct musicexpr_t *me, *next_me;
	struct tqitem_me *p;

	if ((me = malloc(sizeof(struct musicexpr_t))) == NULL) {
		warnx("malloc in musicexpr_tq");
		return NULL;
	}

	assert(me_type == ME_TYPE_SEQUENCE || me_type == ME_TYPE_SIMULTENCE);

	me->me_type = me_type;

	TAILQ_INIT(&me->melist);

	next_me = va_arg(va, struct musicexpr_t *);
	while (next_me != NULL) {
		mdl_log(4, 4, "adding expression %p:\n", next_me);
		musicexpr_log(next_me, 4, 5);
		if ((p = malloc(sizeof(struct tqitem_me))) == NULL) {
			warnx("malloc in musicexpr_tq");
			musicexpr_free(me);
			me = NULL;
			goto finish;
		}

		p->me = next_me;
		TAILQ_INSERT_TAIL(&me->melist, p, tq);

		next_me = va_arg(va, struct musicexpr_t *);
	}

finish:
	va_end(va);

	return me;
}

struct musicexpr_t *
musicexpr_sequence(struct musicexpr_t *next_me, ...)
{
	va_list va;
	struct musicexpr_t *me;

	va_start(va, next_me);
	me = musicexpr_tq(ME_TYPE_SEQUENCE, va);
	va_end(va);

	return me;
}

struct musicexpr_t *
musicexpr_simultence(struct musicexpr_t *next_me, ...)
{
	va_list va;
	struct musicexpr_t *me;

	va_start(va, next_me);
	me = musicexpr_tq(ME_TYPE_SIMULTENCE, va);
	va_end(va);

	return me;
}

struct musicexpr_t *
musicexpr_to_simultence(struct musicexpr_t *me, int level)
{
	struct musicexpr_t *simultence, *noteoffsetexpr;
	struct tqitem_me *subexpr;
	int offset, i;

	simultence = NULL;

	switch (me->me_type) {
        case ME_TYPE_ABSNOTE:
        case ME_TYPE_JOINEXPR:
        case ME_TYPE_RELNOTE:
        case ME_TYPE_REST:
        case ME_TYPE_SEQUENCE:
        case ME_TYPE_SIMULTENCE:
        case ME_TYPE_WITHOFFSET:
		simultence = musicexpr_simultence(me);
		break;
        case ME_TYPE_CHORD:
		noteoffsetexpr = chord_to_noteoffsetexpr(me->chord, level);
		if (noteoffsetexpr == NULL)
			break;
		simultence = musicexpr_to_simultence(noteoffsetexpr, level);
		musicexpr_free(noteoffsetexpr);
		break;
        case ME_TYPE_NOTEOFFSETEXPR:
		simultence = musicexpr_simultence(NULL);

		for (i = 0; i < me->noteoffsetexpr.count; i++) {
			offset = me->noteoffsetexpr.offsets[i];
			subexpr = musicexpr_clone_to_tqitem(me, level);
			if (subexpr == NULL) {
				musicexpr_free(simultence);
				simultence = NULL;
				break;
			}
			apply_noteoffset(subexpr->me, offset, level);
			TAILQ_INSERT_TAIL(&simultence->melist, subexpr, tq);
		}
		break;
	default:
		assert(0);
	}

	if (simultence == NULL)
		warnx("could not convert musicexpr to simultence");

	return simultence;
}

static struct tqitem_me *
musicexpr_clone_to_tqitem(struct musicexpr_t *me, int level)
{
	struct tqitem_me *tqitem;
	struct musicexpr_t *cloned_me;

	if ((cloned_me = musicexpr_clone(me, level)) == NULL)
		return NULL;

	tqitem = malloc(sizeof(struct tqitem_me));
	if (tqitem == NULL) {
		warnx("malloc in musicexpr_clone_to_tqitem");
		musicexpr_free(cloned_me);
		return NULL;
	}

	tqitem->me = cloned_me;

	return tqitem;
}

static void
apply_noteoffset(struct musicexpr_t *me, int offset, int level)
{
	struct tqitem_me *p;

	/* XXX there is probably a common pattern here: do some operation
	 * XXX to all subexpressions... but the knowledge "what are the
	 * XXX subexpressions" is not anywhere */

	assert(me->me_type != ME_TYPE_RELNOTE);

	switch (me->me_type) {
        case ME_TYPE_ABSNOTE:
		me->absnote.note += offset;
		break;
        case ME_TYPE_JOINEXPR:
		apply_noteoffset(me->joinexpr.a, offset, level);
		apply_noteoffset(me->joinexpr.b, offset, level);
		break;
        case ME_TYPE_REST:
		/* do nothing */
		break;
        case ME_TYPE_SEQUENCE:
        case ME_TYPE_SIMULTENCE:
		TAILQ_FOREACH(p, &me->melist, tq)
			apply_noteoffset(p->me, offset, level);
		break;
        case ME_TYPE_WITHOFFSET:
		apply_noteoffset(me->offsetexpr.me, offset, level);
		break;
        case ME_TYPE_CHORD:
		apply_noteoffset(me->chord.me, offset, level);
		break;
        case ME_TYPE_NOTEOFFSETEXPR:
		apply_noteoffset(me->noteoffsetexpr.me, offset, level);
		break;
	default:
		assert(0);
	}
}

void
musicexpr_log(const struct musicexpr_t *me, int loglevel, int indentlevel)
{
	switch (me->me_type) {
	case ME_TYPE_ABSNOTE:
		mdl_log(loglevel,
			indentlevel,
			"absnote notesym=%d note=%d length=%.3f\n",
			me->absnote.notesym,
			me->absnote.note,
			me->absnote.length);
		break;
	case ME_TYPE_CHORD:
		mdl_log(loglevel, indentlevel, "chord\n");
		musicexpr_log_chordtype(me->chord.chordtype,
					loglevel,
					indentlevel + 1);
		musicexpr_log(me->chord.me, loglevel, indentlevel + 1);
		break;
	case ME_TYPE_JOINEXPR:
		mdl_log(loglevel, indentlevel, "joinexpr\n");
		musicexpr_log(me->joinexpr.a, loglevel, indentlevel + 1);
		musicexpr_log(me->joinexpr.b, loglevel, indentlevel + 1);
		break;
	case ME_TYPE_RELNOTE:
		mdl_log(loglevel,
			indentlevel,
			"relnote notesym=%d notemods=%d length=%.3f" \
			  " octavemods=%d\n",
			me->relnote.notesym,
			me->relnote.notemods,
			me->relnote.length,
			me->relnote.octavemods);
		break;
	case ME_TYPE_REST:
		mdl_log(loglevel,
			indentlevel,
			"rest length=%.3f\n",
			me->rest.length);
		break;
	case ME_TYPE_SEQUENCE:
		mdl_log(loglevel, indentlevel, "sequence\n");
		musicexpr_log_melist(me->melist, loglevel, indentlevel);
		break;
	case ME_TYPE_SIMULTENCE:
		mdl_log(loglevel, indentlevel, "simultence\n");
		musicexpr_log_melist(me->melist, loglevel, indentlevel);
		break;
	case ME_TYPE_WITHOFFSET:
		mdl_log(loglevel,
			indentlevel,
			"offsetexpr offset=%.3f\n", me->offsetexpr.offset);
		musicexpr_log(me->offsetexpr.me, loglevel, indentlevel + 1);
		break;
	default:
		assert(0);
	}
}

static void
musicexpr_log_chordtype(enum chordtype_t chordtype,
			int loglevel,
			int indentlevel)
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
		"chordtype %s\n",
		chordnames[chordtype]);
}

static void
musicexpr_log_melist(struct melist_t melist, int loglevel, int indentlevel)
{
	struct tqitem_me *p;

	TAILQ_FOREACH(p, &melist, tq)
		musicexpr_log(p->me, loglevel, indentlevel + 1);
}

void
musicexpr_free(struct musicexpr_t *me)
{
	switch (me->me_type) {
	case ME_TYPE_ABSNOTE:
	case ME_TYPE_RELNOTE:
	case ME_TYPE_REST:
		break;
	case ME_TYPE_JOINEXPR:
		musicexpr_free(me->joinexpr.a);
		musicexpr_free(me->joinexpr.b);
		break;
	case ME_TYPE_SEQUENCE:
		musicexpr_free_melist(me->melist);
		break;
	case ME_TYPE_WITHOFFSET:
		musicexpr_free(me->offsetexpr.me);
		break;
	case ME_TYPE_CHORD:
		musicexpr_free(me->chord.me);
		break;
	case ME_TYPE_NOTEOFFSETEXPR:
		musicexpr_free(me->noteoffsetexpr.me);
		break;
	default:
		assert(0);
	}

	free(me);
}

void
musicexpr_free_melist(struct melist_t melist)
{
	struct tqitem_me *p, *q;

	TAILQ_FOREACH_SAFE(p, &melist, tq, q) {
		TAILQ_REMOVE(&melist, p, tq);
		musicexpr_free(p->me);
		free(p);
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
	struct musicexpr_t *me;
	enum chordtype_t chordtype;

	if ((me = malloc(sizeof(struct musicexpr_t))) == NULL) {
		warn("malloc in chord_to_noteoffsetexpr");
		return NULL;
	}

	chordtype = chord.chordtype;

	assert(chord.me->me_type == ME_TYPE_ABSNOTE);
	assert(0 <= chordtype && chordtype < CHORDTYPE_MAX);

	me->me_type                = ME_TYPE_NOTEOFFSETEXPR;
	me->noteoffsetexpr.me      = musicexpr_clone(chord.me, level);
	me->noteoffsetexpr.count   = chord_noteoffsets[chordtype].count;
	me->noteoffsetexpr.offsets = chord_noteoffsets[chordtype].offsets;

	return me;
}
