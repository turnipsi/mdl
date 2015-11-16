/* $Id: musicexpr.c,v 1.10 2015/11/16 21:05:12 je Exp $ */

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
#include <stdio.h>
#include <stdlib.h>

#include "midi.h"
#include "musicexpr.h"

static struct musicexpr_t	*musicexpr_clone(struct musicexpr_t *);
static struct sequence_t	*musicexpr_clone_sequence(struct sequence_t *);

static void	relative_to_absolute(struct musicexpr_t *,
				     float *,
				     int *,
				     enum notesym_t *);

static int
compare_notesyms(enum notesym_t a, enum notesym_t b);

struct musicexpr_t *
musicexpr_do_joining(struct musicexpr_t *me)
{
	/* XXX */

	return me;
}

struct musicexpr_t *
musicexpr_offsetize(struct musicexpr_t *me)
{
	/* XXX */

	return me;
}

static struct musicexpr_t *
musicexpr_clone(struct musicexpr_t *me)
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
	case ME_TYPE_RELNOTE:
	case ME_TYPE_JOINEXPR:
		break;
	case ME_TYPE_SEQUENCE:
		cloned->sequence = musicexpr_clone_sequence(me->sequence);
		if (cloned->sequence == NULL) {
			free(cloned);
			cloned = NULL;
		}
		break;
	case ME_TYPE_WITHOFFSET:
		cloned->offset_expr.me = musicexpr_clone(me->offset_expr.me);
		if (cloned->offset_expr.me == NULL) {
			warn("malloc failure when cloning musicexpr" \
			       " with offset");
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
musicexpr_clone_sequence(struct sequence_t *seq)
{
	struct sequence_t *p, *q, *prev_q, *new;

	new = malloc(sizeof(struct sequence_t));
	if (new == NULL) {
		warn("malloc failure when cloning sequence");
		return NULL;
	}
	prev_q = new;

	for (p = seq; p != NULL; p = p->next) {
		q = malloc(sizeof(struct sequence_t));
		if (q == NULL) {
			warn("malloc failure when cloning sequence");
			musicexpr_free_sequence(new);
			return NULL;
		}
		q->me = musicexpr_clone(p->me);
		q->next = NULL;
		prev_q->next = q;
		prev_q = q;
	}

	return new;
}

struct musicexpr_t *
musicexpr_relative_to_absolute(struct musicexpr_t *rel_me)
{
	struct musicexpr_t *abs_me;
	float prev_length;
	int prev_note;
	enum notesym_t prev_notesym;

	if ((abs_me = musicexpr_clone(rel_me)) == NULL)
		return NULL;

	/* set default values for the first relative note */
	prev_length = 0.25;
	prev_note = 60;
	prev_notesym = NOTE_C;

	relative_to_absolute(abs_me, &prev_length, &prev_note, &prev_notesym);

	return abs_me;
}

static void
relative_to_absolute(struct musicexpr_t *me,
		     float *prev_length,
		     int *prev_note,
		     enum notesym_t *prev_notesym)
{
	struct sequence_t *seq;
	struct absnote_t absnote;
	struct relnote_t relnote;
	int notevalues[] = {
		/* for NOTE_C, NOTE_D, ... */
		0, 2, 4, 5, 7, 9, 11,
	};
	int note, c;

	assert(me->me_type != ME_TYPE_ABSNOTE);
	assert(me->me_type != ME_TYPE_JOINEXPR);

	switch (me->me_type) {
	case ME_TYPE_RELNOTE:
		relnote = me->relnote;

		assert(0 <= relnote.notesym && relnote.notesym < NOTE_MAX);
		assert(relnote.length >= 0);

		note = 12 * (*prev_note / 12)
			 + notevalues[relnote.notesym]
			 + relnote.notemods;

		c = compare_notesyms(*prev_notesym, me->relnote.notesym);
		if (c > 0 && *prev_note > note) {
			note += 12;
		} else if (c < 0 && *prev_note < note) {
			note -= 12;
		}

		note += 12 * relnote.octavemods;

		/* XXX relnote should be converted to rest instead of
		 * XXX an absolute note in case our range is exhausted */
		assert(0 <= note && note <= MIDI_NOTE_MAX);

		absnote.note = note;
		*prev_note = note;

		if (relnote.length > 0)
			*prev_length = relnote.length;
		absnote.length = *prev_length;

		break;
	case ME_TYPE_SEQUENCE:
		for (seq = me->sequence; seq != NULL; seq = seq->next) {
			relative_to_absolute(seq->me,
					     prev_length,
					     prev_note,
					     prev_notesym);
		}
		break;
	case ME_TYPE_WITHOFFSET:
		relative_to_absolute(me->offset_expr.me,
				     prev_length,
				     prev_note,
				     prev_notesym);
		break;
	default:
		assert(0);
		break;
	}
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

struct midievent *
musicexpr_to_midievents(struct musicexpr_t *me)
{
	struct midievent *events;

	/* XXX */
	events = NULL;

	return events; 
}

int
musicexpr_print(int indentlevel, struct musicexpr_t *me)
{
	int i, ret;

	for (i = 0; i < indentlevel; i++) {
		ret = printf(" ");
		if (ret < 0)
			return ret;
	}

	switch (me->me_type) {
	case ME_TYPE_ABSNOTE:
		ret = printf("absnote note=%d length=%f\n",
			     me->absnote.note,
			     me->absnote.length);
		break;
	case ME_TYPE_RELNOTE:
		ret = printf("relnote notesym=%d notemods=%d" \
			       " length=%f octavemods=%d\n",
			     me->relnote.notesym,
			     me->relnote.notemods,
			     me->relnote.length,
			     me->relnote.octavemods);
		break;
	case ME_TYPE_SEQUENCE:
		ret = printf("sequence\n");
		if (ret < 0)
			break;
		ret = musicexpr_print_sequence(indentlevel + 2,
					       me->sequence);
		break;
	case ME_TYPE_WITHOFFSET:
		ret = printf("offset_expr offset=%f\n",
			     me->offset_expr.offset);
		if (ret < 0)
			break;
		ret = musicexpr_print(indentlevel + 2, me->offset_expr.me);
		break;
	case ME_TYPE_JOINEXPR:
		ret = printf("joinexpr\n");
		break;
	default:
		assert(0);
	}

	return ret;
}

int
musicexpr_print_sequence(int indentlevel, struct sequence_t *seq)
{
	struct sequence_t *p;
	int ret;

	ret = 0;

	for (p = seq; p != NULL; p = p->next) {
		ret = musicexpr_print(indentlevel, p->me);
		if (ret < 0)
			break;
	}

	return ret;
}

void
musicexpr_free(struct musicexpr_t *me)
{
	switch (me->me_type) {
	case ME_TYPE_ABSNOTE:
	case ME_TYPE_RELNOTE:
	case ME_TYPE_JOINEXPR:
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
