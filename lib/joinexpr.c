/* $Id: joinexpr.c,v 1.6 2015/12/15 21:12:03 je Exp $ */

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

static int	join_joinexpr(struct musicexpr_t *, int);

static struct musicexpr_t *
join_sequences(struct musicexpr_t *, struct musicexpr_t *, int);

int
joinexpr_musicexpr(struct musicexpr_t *me, int level)
{
	struct seqitem *s;
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

		ret = join_joinexpr(me, level + 1);
		break;
	case ME_TYPE_SEQUENCE:
		TAILQ_FOREACH(s, &me->sequence, tq) {
			ret = joinexpr_musicexpr(s->me, level + 1);
			if (ret != 0)
				break;
		}
		break;
	case ME_TYPE_WITHOFFSET:
		ret = joinexpr_musicexpr(me->offset_expr.me, level + 1);
		break;
	case ME_TYPE_CHORD:
		/* the possibly subexpressions of chords
		 * are such that do not need joining */
		assert(me->chord.me->me_type == ME_TYPE_ABSNOTE
			 || me->chord.me->me_type == ME_TYPE_RELNOTE);
		break;
	default:
		assert(0);
	}

	return ret;
}

static int
join_joinexpr(struct musicexpr_t *me, int level)
{
	struct musicexpr_t *a, *b, *new_me;

	assert(me->me_type == ME_TYPE_JOINEXPR);

	new_me = NULL;

	a = me->joinexpr.a;
	b = me->joinexpr.b;

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
		default:
			assert(0);
		}
		break;
	case ME_TYPE_REST:
		switch (b->me_type) {
		case ME_TYPE_ABSNOTE:
			new_me = musicexpr_sequence(a, b, NULL);
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
		case ME_TYPE_SEQUENCE:
			new_me = join_sequences(a, b, level);
			break;
		case ME_TYPE_WITHOFFSET:
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
		case ME_TYPE_REST:
			unimplemented();
			break;
		case ME_TYPE_SEQUENCE:
			unimplemented();
			break;
		case ME_TYPE_WITHOFFSET:
			unimplemented();
			break;
		default:
			assert(0);
		}
		break;
	default:
		assert(0);
	}

	if (new_me == NULL)
		return 1;

	*me = *new_me;

	free(new_me);

	return 0;
}

static struct musicexpr_t *
join_sequences(struct musicexpr_t *a, struct musicexpr_t *b, int level)
{
	struct seqitem *last_of_a, *first_of_b, *s;
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

	s = malloc(sizeof(struct seqitem));
	if (s == NULL) {
		warn("malloc in join_sequences");
		free(joined_expr);
		return NULL;
	}
	s->me = joined_expr;

	join_joinexpr(joined_expr, level + 1);

	TAILQ_REMOVE(&b->sequence, first_of_b, tq);
	TAILQ_REPLACE(&a->sequence, last_of_a, s, tq);
	TAILQ_CONCAT(&a->sequence, &b->sequence, tq);

	musicexpr_free(b);

	return a;
}
