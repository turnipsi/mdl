/* $Id: musicexpr.c,v 1.8 2015/11/12 21:30:57 je Exp $ */

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
musicexpr_relative_to_absolute(struct musicexpr_t *me)
{
	/* XXX */

	return me;
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
		ret = printf("absnote note=%d length=%f time=%f\n",
			     me->absnote.note,
			     me->absnote.length,
			     me->absnote.time_as_measures);
		break;
	case ME_TYPE_RELNOTE:
		ret = printf("relnote notesym=%d length=%f octavemods=%d\n",
			     me->relnote.notesym,
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
