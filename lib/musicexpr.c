/* $Id: musicexpr.c,v 1.27 2015/12/09 19:56:10 je Exp $ */

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
static struct sequence_t	*musicexpr_clone_sequence(struct sequence_t *,
							  int);

static struct mdl_stream *offsetexprstream_new(void);

static void	musicexpr_relative_to_absolute(struct musicexpr_t *, int);

static void	relative_to_absolute(struct musicexpr_t *,
				     struct absnote_t *,
				     int);
static int	musicexpr_flatten(struct mdl_stream *, struct musicexpr_t *);
static int	offset_expressions(struct mdl_stream *,
				   struct musicexpr_t *,
				   float *,
				   int);

static int	add_new_offset_expression(struct mdl_stream *,
					  struct musicexpr_t *,
					  float offset);

static struct mdl_stream *
offsetexprstream_to_midievents(const struct mdl_stream *);

static int
compare_notesyms(enum notesym_t a, enum notesym_t b);

static int
musicexpr_flatten(struct mdl_stream *oes, struct musicexpr_t *me)
{
	float offset;

	offset = 0.0;

	mdl_log(1, 0, "flattening music expression\n");
	mdl_log(2, 1, "initializing offset to %.3f\n", offset);

	if (offset_expressions(oes, me, &offset, 1) != 0)
		return 1;

	return 0;
}

static int
offset_expressions(struct mdl_stream *oes,
		   struct musicexpr_t *me,
		   float *offset,
		   int level)
{
	struct sequence_t *seq;
	float old_offset;
	int ret;

	assert(me->me_type != ME_TYPE_RELNOTE
		 && me->me_type != ME_TYPE_JOINEXPR);

	old_offset = *offset;

	switch (me->me_type) {
	case ME_TYPE_ABSNOTE:
		mdl_log(3, level, "finding offset expression for absnote\n");
		if ((ret = add_new_offset_expression(oes, me, *offset)) != 0)
			return ret;
		*offset += me->absnote.length;
		break;
	case ME_TYPE_REST:
		mdl_log(3, level, "finding offset expression for rest\n");
		*offset += me->rest.length;
		break;
	case ME_TYPE_SEQUENCE:
		mdl_log(3, level, "finding offset expression for sequence\n");
		for (seq = me->sequence; seq != NULL; seq = seq->next) {
			ret = offset_expressions(oes,
						 seq->me,
						 offset,
						 level + 1);
			if (ret != 0)
				return ret;
		}
		break;
	case ME_TYPE_WITHOFFSET:
		mdl_log(3, level, "finding offset expression for" \
				    " offset expression\n");
		*offset += me->offset_expr.offset;
		ret = offset_expressions(oes,
					 me->offset_expr.me,
					 offset,
					 level + 1);
		if (ret != 0)
			return ret;
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
		cloned->sequence = musicexpr_clone_sequence(me->sequence,
							    level + 1);
		if (cloned->sequence == NULL) {
			free(cloned);
			cloned = NULL;
		}
		break;
	case ME_TYPE_WITHOFFSET:
		mdl_log(4, level, "cloning expression %p (withoffset)\n", me);
		musicexpr_log(me, 4, level + 1);
		cloned->offset_expr.me = musicexpr_clone(me->offset_expr.me,
							 level + 1);
		if (cloned->offset_expr.me == NULL) {
			free(cloned);
			cloned = NULL;
		}
		break;
	default:
		assert(0);
	}

	return cloned;
}

static struct sequence_t *
musicexpr_clone_sequence(struct sequence_t *seq, int level)
{
	struct sequence_t *p, *q, *prev_q, *new;

	new = NULL;
	prev_q = NULL;

	for (p = seq; p != NULL; p = p->next) {
		q = malloc(sizeof(struct sequence_t));
		if (q == NULL) {
			warn("malloc failure when cloning sequence");
			if (new != NULL)
				musicexpr_free_sequence(new);
			return NULL;
		}
		q->me = musicexpr_clone(p->me, level);
		if (q->me == NULL) {
			free(q);
			if (new != NULL)
				musicexpr_free_sequence(new);
			return NULL;
		}
		q->next = NULL;
		if (new == NULL)
			new = q;
		if (prev_q != NULL)
			prev_q->next = q;
		prev_q = q;
	}

	return new;
}

static void
musicexpr_relative_to_absolute(struct musicexpr_t *me, int level)
{
	struct absnote_t prev_absnote;

	mdl_log(2, level, "converting relative expression to absolute\n");

	/* set default values for the first absolute note */
	prev_absnote.length = 0.25;
	prev_absnote.notesym = NOTE_C;
	prev_absnote.note = 60;

	relative_to_absolute(me, &prev_absnote, level + 1);
}

static void
relative_to_absolute(struct musicexpr_t *me, struct absnote_t *prev_absnote,
		     int level)
{
	struct sequence_t *seq;
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
		*prev_absnote = me->absnote;
		break;
	case ME_TYPE_RELNOTE:
		mdl_log(3, level, "rel->abs expression (relnote)\n");
		musicexpr_log(me, 3, level + 1);
		relnote = me->relnote;

		assert(0 <= relnote.notesym && relnote.notesym < NOTE_MAX);
		assert(relnote.length >= 0);

		note = 12 * (prev_absnote->note / 12)
			 + notevalues[relnote.notesym]
			 + relnote.notemods;

		c = compare_notesyms(prev_absnote->notesym, relnote.notesym);
		if (c > 0 && prev_absnote->note > note) {
			note += 12;
		} else if (c < 0 && prev_absnote->note < note) {
			note -= 12;
		}

		note += 12 * relnote.octavemods;

		absnote.notesym = relnote.notesym;
		absnote.length  = relnote.length;
		if (absnote.length == 0)
			absnote.length = prev_absnote->length;
		absnote.note = note;

		me->me_type = ME_TYPE_ABSNOTE;
		me->absnote = absnote;

		*prev_absnote = absnote;

		break;
	case ME_TYPE_REST:
		mdl_log(3, level, "rel->abs expression (rest)\n");
		musicexpr_log(me, 3, level + 1);

		if (me->rest.length == 0) {
			me->rest.length = prev_absnote->length;
		} else {
			prev_absnote->length = me->rest.length;
		}
		break;
	case ME_TYPE_JOINEXPR:
		mdl_log(3, level, "rel->abs expression (joinexpr)\n");
		relative_to_absolute(me->joinexpr.a, prev_absnote, level + 1);
		relative_to_absolute(me->joinexpr.b, prev_absnote, level + 1);
		break;
	case ME_TYPE_SEQUENCE:
		mdl_log(3, level, "rel->abs expression (sequence)\n");
		for (seq = me->sequence; seq != NULL; seq = seq->next)
			relative_to_absolute(seq->me,
					     prev_absnote,
					     level + 1);
		break;
	case ME_TYPE_WITHOFFSET:
		mdl_log(3, level, "rel->abs expression (withoffset)\n");
		relative_to_absolute(me->offset_expr.me,
				     prev_absnote,
				     level + 1);
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

	if ((midi_es = offsetexprstream_to_midievents(offset_es)) == NULL)
		warnx("could not convert offset-expression-stream" \
			" to midistream");

finish:
	mdl_stream_free(offset_es);
	musicexpr_free(me_workcopy);

	return midi_es;
}

struct mdl_stream *
offsetexprstream_to_midievents(const struct mdl_stream *offset_es)
{
	struct mdl_stream *midi_es;
	struct midievent *midievent;
	struct musicexpr_with_offset_t offset_expr;
	struct musicexpr_t *me;
	float offset;
	int i, ret;

	if ((midi_es = midi_eventstream_new()) == NULL)
		goto error;

	for (i = 0; i < offset_es->count; i++) {
		offset_expr = offset_es->mexprs[i];
		me = offset_expr.me;
		offset = offset_expr.offset;

		assert(me->me_type == ME_TYPE_ABSNOTE);

		if (me->absnote.note < 0 || MIDI_NOTE_MAX < me->absnote.note) {
			warnx("skipping note with value %d", me->absnote.note);
			continue;
		}

		if (me->absnote.length < 0) {
			warnx("skipping note with length %.3f",
			      me->absnote.length);
			continue;
		}

		midievent = &midi_es->midievents[ midi_es->count ];
		bzero(midievent, sizeof(struct midievent));
		midievent->eventtype = NOTEON;
		midievent->channel = DEFAULT_MIDICHANNEL;
		midievent->note = me->absnote.note;
		midievent->time_as_measures = offset;
		midievent->velocity = DEFAULT_VELOCITY;

		ret = mdl_stream_increment(midi_es);
		if (ret != 0)
			goto error;

		midievent = &midi_es->midievents[ midi_es->count ];
		bzero(midievent, sizeof(struct midievent));
		midievent->eventtype = NOTEOFF;
		midievent->channel = DEFAULT_MIDICHANNEL;
		midievent->note = me->absnote.note;
		midievent->time_as_measures = offset + me->absnote.length;
		midievent->velocity = 0;

		ret = mdl_stream_increment(midi_es);
		if (ret != 0)
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

void
musicexpr_log(struct musicexpr_t *me, int loglevel, int indentlevel)
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
	case ME_TYPE_RELNOTE:
		mdl_log(loglevel,
			indentlevel,
			"relnote notesym=%d notemods=%d" \
			  " length=%.3f octavemods=%d\n",
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
		musicexpr_log_sequence(me->sequence, loglevel, indentlevel);
		break;
	case ME_TYPE_WITHOFFSET:
		mdl_log(loglevel,
			indentlevel,
			"offset_expr offset=%.3f\n", me->offset_expr.offset);
		musicexpr_log(me->offset_expr.me, loglevel, indentlevel + 1);
		break;
	case ME_TYPE_JOINEXPR:
		mdl_log(loglevel, indentlevel, "joinexpr\n");
		musicexpr_log(me->joinexpr.a, loglevel, indentlevel + 1);
		musicexpr_log(me->joinexpr.b, loglevel, indentlevel + 1);
		break;
	default:
		assert(0);
	}
}

void
musicexpr_log_sequence(struct sequence_t *seq, int loglevel, int indentlevel)
{
	struct sequence_t *p;

	for (p = seq; p != NULL; p = p->next)
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
		musicexpr_free_sequence(me->sequence);
		break;
	case ME_TYPE_WITHOFFSET:
		musicexpr_free(me->offset_expr.me);
		break;
	default:
		assert(0);
	}

	free(me);
}

void
musicexpr_free_sequence(struct sequence_t *seq)
{
	struct sequence_t *p, *q;

	p = seq;
	while (p) {
		q = p;
		p = p->next;
		musicexpr_free(q->me);
	}
}

static struct mdl_stream *
offsetexprstream_new(void)
{
	return mdl_stream_new(OFFSETEXPRSTREAM);
}
