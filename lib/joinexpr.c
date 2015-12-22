/* $Id: joinexpr.c,v 1.11 2015/12/22 20:23:52 je Exp $ */

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
#include <stdlib.h>

#include "joinexpr.h"
#include "musicexpr.h"

int		joinexpr_musicexpr(struct musicexpr_t *, int);
static struct	musicexpr_t *join_two_musicexprs(struct musicexpr_t *,
						 struct musicexpr_t *,
						 int);

static struct musicexpr_t *
join_sequences(struct musicexpr_t *, struct musicexpr_t *, int);

int
joinexpr_musicexpr(struct musicexpr_t *me, int level)
{
	struct musicexpr_t *joined_me;
	struct tqitem_me *p;
	int ret;

	ret = 0;

	switch (me->me_type) {
	case ME_TYPE_ABSNOTE:
	case ME_TYPE_RELNOTE:
	case ME_TYPE_REST:
		break;
	case ME_TYPE_JOINEXPR:
		ret = joinexpr_musicexpr(me->joinexpr.a, level + 1);
		if (ret != 0)
			break;
		ret = joinexpr_musicexpr(me->joinexpr.b, level + 1);
		if (ret != 0)
			break;

		joined_me = join_two_musicexprs(me->joinexpr.a,
						me->joinexpr.b,
						level + 1);
		if (joined_me == NULL) {
			ret = 1;
			break;
		}
		*me = *joined_me;
		break;
	case ME_TYPE_SEQUENCE:
		TAILQ_FOREACH(p, &me->sequence, tq) {
			ret = joinexpr_musicexpr(p->me, level + 1);
			if (ret != 0)
				break;
		}
		break;
	case ME_TYPE_WITHOFFSET:
		ret = joinexpr_musicexpr(me->offsetexpr.me, level + 1);
		break;
	case ME_TYPE_CHORD:
		/* the possible subexpressions of chords are such that
		 * calling joinexpr_musicexpr() is a no-op */
		assert(me->chord.me->me_type == ME_TYPE_ABSNOTE
			 || me->chord.me->me_type == ME_TYPE_RELNOTE);
		break;
	default:
		assert(0);
	}

	return ret;
}

/* frees or reuses a and b, those should be used after passing through
 * this function */
static struct musicexpr_t *
join_two_musicexprs(struct musicexpr_t *a, struct musicexpr_t *b, int level)
{
	struct musicexpr_t *new_me;

	new_me = NULL;

	/* Relative notes can not be joined (think of case "cis ~ des g"...
	 * if "des" disappears then (absolute note) "g" gets resolved
	 * differently).  Thus it is callers responsibility to call
	 * musicexpr_relative_to_absolute() for both a and b before calling
	 * this. */
	assert(a->me_type != ME_TYPE_RELNOTE);
	assert(b->me_type != ME_TYPE_RELNOTE);

	/* we should have handled the subexpressions before entering here */
	assert(a->me_type != ME_TYPE_JOINEXPR);
	assert(b->me_type != ME_TYPE_JOINEXPR);

	if (a->me_type == ME_TYPE_CHORD)
		assert(a->chord.me->me_type == ME_TYPE_ABSNOTE);
	if (b->me_type == ME_TYPE_CHORD)
		assert(b->chord.me->me_type == ME_TYPE_ABSNOTE);

	switch (a->me_type) {
	case ME_TYPE_ABSNOTE:
		switch (b->me_type) {
		case ME_TYPE_ABSNOTE:
			if (a->absnote.note == b->absnote.note) {
				new_me = a;
				new_me->absnote.length += b->absnote.length;
				musicexpr_free(b);
			} else {
				new_me = musicexpr_sequence(a, b, NULL);
			}
			break;
		case ME_TYPE_CHORD:
			unimplemented();
			break;
		case ME_TYPE_REST:
			new_me = musicexpr_sequence(a, b, NULL);
			break;
		case ME_TYPE_SEQUENCE:
			new_me = join_sequences(musicexpr_sequence(a, NULL),
						b,
						level);
			break;
		case ME_TYPE_WITHOFFSET:
			unimplemented();
			break;
		case ME_TYPE_NOTEOFFSETEXPR:
			unimplemented();
			break;
		default:
			assert(0);
		}
		break;
	case ME_TYPE_CHORD:
		switch (b->me_type) {
		case ME_TYPE_ABSNOTE:
			unimplemented();
			break;
		case ME_TYPE_CHORD:
			if (a->chord.chordtype == b->chord.chordtype
			      && a->chord.me->absnote.note
				   == b->chord.me->absnote.note) {
				new_me = a;
				new_me->chord.me->absnote.length
				    += b->chord.me->absnote.length;
				musicexpr_free(b);
				break;
			}
			new_me = join_two_musicexprs(
				   chord_to_noteoffsetexpr(a->chord, level),
				   chord_to_noteoffsetexpr(b->chord, level),
				   level);
			break;
		case ME_TYPE_REST:
			unimplemented();
			break;
		case ME_TYPE_SEQUENCE:
			unimplemented();
			break;
		case ME_TYPE_WITHOFFSET:
			unimplemented();
			break;
		case ME_TYPE_NOTEOFFSETEXPR:
			unimplemented();
			break;
		default:
			assert(0);
		}
		break;
	case ME_TYPE_REST:
		switch (b->me_type) {
		case ME_TYPE_ABSNOTE:
			new_me = musicexpr_sequence(a, b, NULL);
			break;
		case ME_TYPE_CHORD:
			unimplemented();
			break;
		case ME_TYPE_REST:
			new_me = a;
			new_me->rest.length += b->rest.length;
			musicexpr_free(b);
			break;
		case ME_TYPE_SEQUENCE:
			new_me = join_sequences(musicexpr_sequence(a, NULL),
						b,
						level);
			break;
		case ME_TYPE_WITHOFFSET:
			unimplemented();
			break;
		case ME_TYPE_NOTEOFFSETEXPR:
			unimplemented();
			break;
		default:
			assert(0);
		}
		break;
	case ME_TYPE_SEQUENCE:
		switch (b->me_type) {
		case ME_TYPE_ABSNOTE:
		case ME_TYPE_REST:
			new_me = join_sequences(a,
						musicexpr_sequence(b, NULL),
						level);
			break;
		case ME_TYPE_CHORD:
			unimplemented();
			break;
		case ME_TYPE_SEQUENCE:
			new_me = join_sequences(a, b, level);
			break;
		case ME_TYPE_WITHOFFSET:
			unimplemented();
			break;
		case ME_TYPE_NOTEOFFSETEXPR:
			unimplemented();
			break;
		default:
			assert(0);
		}
		break;
	case ME_TYPE_WITHOFFSET:
		switch (b->me_type) {
		case ME_TYPE_ABSNOTE:
			unimplemented();
			break;
		case ME_TYPE_CHORD:
			unimplemented();
			break;
		case ME_TYPE_REST:
			unimplemented();
			break;
		case ME_TYPE_SEQUENCE:
			unimplemented();
			break;
		case ME_TYPE_WITHOFFSET:
			unimplemented();
			break;
		case ME_TYPE_NOTEOFFSETEXPR:
			unimplemented();
			break;
		default:
			assert(0);
		}
		break;
	case ME_TYPE_NOTEOFFSETEXPR:
		switch (b->me_type) {
		case ME_TYPE_ABSNOTE:
			unimplemented();
			break;
		case ME_TYPE_CHORD:
			unimplemented();
			break;
		case ME_TYPE_REST:
			unimplemented();
			break;
		case ME_TYPE_SEQUENCE:
			unimplemented();
			break;
		case ME_TYPE_WITHOFFSET:
			unimplemented();
			break;
		case ME_TYPE_NOTEOFFSETEXPR:
			unimplemented();
			break;
		default:
			assert(0);
		}
		break;
	default:
		assert(0);
	}

	return new_me;
}

static struct musicexpr_t *
join_sequences(struct musicexpr_t *a, struct musicexpr_t *b, int level)
{
	struct tqitem_me *last_of_a, *first_of_b, *p;
	struct musicexpr_t *joined_expr;

	if (a == NULL || b == NULL) {
		if (a != NULL)
			musicexpr_free(a);
		if (b != NULL)
			musicexpr_free(b);
		return NULL;
	}

	assert(a->me_type == ME_TYPE_SEQUENCE);
	assert(b->me_type == ME_TYPE_SEQUENCE);

	if (TAILQ_EMPTY(&a->sequence)) {
		musicexpr_free(a);
		return b;
	}

	if (TAILQ_EMPTY(&b->sequence)) {
		musicexpr_free(b);
		return a;
	}

	last_of_a = TAILQ_LAST(&a->sequence, sequence_t);
	first_of_b = TAILQ_FIRST(&b->sequence);

	joined_expr = malloc(sizeof(struct musicexpr_t));
	if (joined_expr == NULL) {
		warn("malloc in join_sequences");
		return NULL;
	}
	joined_expr->me_type = ME_TYPE_JOINEXPR;
	joined_expr->joinexpr.a = last_of_a->me;
	joined_expr->joinexpr.b = first_of_b->me;

	p = malloc(sizeof(struct tqitem_me));
	if (p == NULL) {
		warn("malloc in join_sequences");
		free(joined_expr);
		return NULL;
	}
	p->me = joined_expr;

	joinexpr_musicexpr(joined_expr, level + 1);

	TAILQ_REMOVE(&b->sequence, first_of_b, tq);
	TAILQ_REPLACE(&a->sequence, last_of_a, p, tq);
	TAILQ_CONCAT(&a->sequence, &b->sequence, tq);

	musicexpr_free(b);

	return a;
}
