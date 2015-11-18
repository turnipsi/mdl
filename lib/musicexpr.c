/* $Id: musicexpr.c,v 1.13 2015/11/18 21:15:59 je Exp $ */

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
#include "util.h"

static struct musicexpr_t	*musicexpr_clone(struct musicexpr_t *);
static struct sequence_t	*musicexpr_clone_sequence(struct sequence_t *);

static void	relative_to_absolute(struct musicexpr_t *, struct absnote_t *);

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
		mdl_log(2, "cloning expression %p (absnote)\n");
		break;
	case ME_TYPE_RELNOTE:
		mdl_log(2, "cloning expression %p (relnote)\n");
		break;
	case ME_TYPE_JOINEXPR:
		mdl_log(2, "cloning expression %p (joinexpr)\n");
		break;
	case ME_TYPE_SEQUENCE:
		mdl_log(2, "cloning expression %p (sequence)\n");
		cloned->sequence = musicexpr_clone_sequence(me->sequence);
		if (cloned->sequence == NULL) {
			free(cloned);
			cloned = NULL;
		}
		break;
	case ME_TYPE_WITHOFFSET:
		mdl_log(2, "cloning expression %p (withoffset)\n");
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
	new->me = musicexpr_clone(seq->me);
	if (new->me == NULL) {
		free(new);
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
		if (q->me == NULL) {
			musicexpr_free_sequence(new);
			return NULL;
		}
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
	struct absnote_t prev_absnote;

	mdl_log(2, "converting relative expression to absolute\n");

	if ((abs_me = musicexpr_clone(rel_me)) == NULL)
		return NULL;

	(void) musicexpr_log(0, abs_me);

	/* set default values for the first absolute note */
	prev_absnote.length = 0.25;
	prev_absnote.notesym = NOTE_C;
	prev_absnote.note = 60;

	relative_to_absolute(abs_me, &prev_absnote);

	return abs_me;
}

static void
relative_to_absolute(struct musicexpr_t *me, struct absnote_t *prev_absnote)
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
		mdl_log(2, "rel->abs expression (absnote)\n");
		/* pass as is, but this affects previous absnote */
		*prev_absnote = me->absnote;
		break;
	case ME_TYPE_RELNOTE:
		mdl_log(2, "rel->abs expression (relnote)\n");
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
	case ME_TYPE_SEQUENCE:
		mdl_log(2, "rel->abs expression (sequence)\n");
		for (seq = me->sequence; seq != NULL; seq = seq->next) {
			relative_to_absolute(seq->me, prev_absnote);
		}
		break;
	case ME_TYPE_WITHOFFSET:
		mdl_log(2, "rel->abs expression (withoffset)\n");
		relative_to_absolute(me->offset_expr.me, prev_absnote);
		break;
	case ME_TYPE_JOINEXPR:
		mdl_log(2, "rel->abs expression (joinexpr)\n");
		/* just pass as is */
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
musicexpr_log(int indentlevel, struct musicexpr_t *me)
{
	int i, ret;

	for (i = 0; i < indentlevel; i++) {
		ret = mdl_log(1, " ");
		if (ret < 0)
			return ret;
	}

	switch (me->me_type) {
	case ME_TYPE_ABSNOTE:
		ret = mdl_log(1,
			      "absnote notesym=%d note=%d length=%f\n",
			      me->absnote.notesym,
			      me->absnote.note,
			      me->absnote.length);
		break;
	case ME_TYPE_RELNOTE:
		ret = mdl_log(1,
			      "relnote notesym=%d notemods=%d" \
			        " length=%f octavemods=%d\n",
			      me->relnote.notesym,
			      me->relnote.notemods,
			      me->relnote.length,
			      me->relnote.octavemods);
		break;
	case ME_TYPE_SEQUENCE:
		ret = mdl_log(1, "sequence\n");
		if (ret < 0)
			break;
		ret = musicexpr_log_sequence(indentlevel + 2, me->sequence);
		break;
	case ME_TYPE_WITHOFFSET:
		ret = mdl_log(1,
			      "offset_expr offset=%f\n",
			      me->offset_expr.offset);
		if (ret < 0)
			break;
		ret = musicexpr_log(indentlevel + 2, me->offset_expr.me);
		break;
	case ME_TYPE_JOINEXPR:
		ret = mdl_log(1, "joinexpr\n");
		break;
	default:
		assert(0);
	}

	return ret;
}

int
musicexpr_log_sequence(int indentlevel, struct sequence_t *seq)
{
	struct sequence_t *p;
	int ret;

	ret = 0;

	for (p = seq; p != NULL; p = p->next) {
		ret = musicexpr_log(indentlevel, p->me);
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
