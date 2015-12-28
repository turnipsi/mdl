/* $Id: joinexpr.c,v 1.15 2015/12/28 21:36:31 je Exp $ */

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

static struct musicexpr_t *
join_simultences(struct musicexpr_t *, struct musicexpr_t *, int);

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
		TAILQ_FOREACH(p, &me->melist, tq) {
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
	struct musicexpr_t *new_me, *tmp_me;
	enum musicexpr_type at, bt;

	new_me = NULL;

	if (a == NULL || b == NULL) {
		if (a != NULL)
			musicexpr_free(a);
		if (b != NULL)
			musicexpr_free(b);
		return NULL;
	}

	at = a->me_type;
	bt = b->me_type;

	/* Relative notes can not be joined (think of case "cis ~ des g"...
	 * if "des" disappears then (absolute note) "g" gets resolved
	 * differently).  Thus it is callers responsibility to call
	 * musicexpr_relative_to_absolute() for both a and b before calling
	 * this. */
	assert(at != ME_TYPE_RELNOTE);
	assert(bt != ME_TYPE_RELNOTE);

	/* we should have handled the subexpressions before entering here */
	assert(at != ME_TYPE_JOINEXPR);
	assert(bt != ME_TYPE_JOINEXPR);

	if (at == ME_TYPE_CHORD)
		assert(a->chord.me->me_type == ME_TYPE_ABSNOTE);
	if (bt == ME_TYPE_CHORD)
		assert(b->chord.me->me_type == ME_TYPE_ABSNOTE);

	if (at == bt) {
		/* when joined music expression types match */
		switch (at) {
		case ME_TYPE_ABSNOTE:
			mdl_log(3, level, "joining two absnotes\n");
			if (a->absnote.note == b->absnote.note) {
				mdl_log(4, level + 1, "matched\n");
				new_me = a;
				new_me->absnote.length += b->absnote.length;
				musicexpr_free(b);
				break;
			}
			mdl_log(4, level + 1, "no match\n");
			break;
		case ME_TYPE_CHORD:
			mdl_log(3, level, "joining two chords\n");
			if (a->chord.chordtype == b->chord.chordtype
			      && a->chord.me->absnote.note
				   == b->chord.me->absnote.note) {
				mdl_log(4, level + 1, "matched\n");
				new_me = a;
				new_me->chord.me->absnote.length
				    += b->chord.me->absnote.length;
				musicexpr_free(b);
				break;
			}
			mdl_log(4, level + 1, "no match\n");
			break;
		case ME_TYPE_NOTEOFFSETEXPR:
			mdl_log(3, level, "joining two noteoffsetexprs\n");
			unimplemented();
			break;
		case ME_TYPE_REST:
			mdl_log(3, level, "joining two rests\n");
			new_me = a;
			new_me->rest.length += b->rest.length;
			musicexpr_free(b);
			break;
		case ME_TYPE_SEQUENCE:
			mdl_log(3, level, "joining two sequences\n");
			new_me = join_sequences(a, b, level);
			break;
		case ME_TYPE_SIMULTENCE:
			mdl_log(3, level, "joining two simultences\n");
			new_me = join_simultences(a, b, level);
			break;
		case ME_TYPE_WITHOFFSET:
			mdl_log(3, level, "joining two exprs with offset\n");
			unimplemented();
			break;
		default:
			assert(0);
			break;
		}
	}

	if (new_me == NULL) {
		/* when joined music expression types do not match */

		if (at == ME_TYPE_REST || bt == ME_TYPE_REST) {
			/* rests are incompatible with everything else */
			mdl_log(4,
				level,
				"joining rest and something --> sequence\n");
			musicexpr_log(a, 4, level + 1);
			musicexpr_log(b, 4, level + 1);
			mdl_log(4, level, "sequence: %p + %p\n", a, b);
			new_me = musicexpr_sequence(a, b, NULL);
			musicexpr_log(new_me, 4, level + 1);
		} else if (at == ME_TYPE_SEQUENCE || bt == ME_TYPE_SEQUENCE) {
			/* non-sequence --> sequence and join sequences */
			mdl_log(4,
				level,
				"wrapping an expression to sequence\n");
			if (at != ME_TYPE_SEQUENCE)
				a = musicexpr_sequence(a);
			if (bt != ME_TYPE_SEQUENCE)
				b = musicexpr_sequence(b);
			new_me = join_two_musicexprs(a, b, level);
		} else if (at == ME_TYPE_CHORD || bt == ME_TYPE_CHORD) {
			/* chord --> noteoffsetexpr and join */
			mdl_log(4,
				level,
				"converting chord to noteoffsetexpr\n");
			if (at == ME_TYPE_CHORD) {
				tmp_me = chord_to_noteoffsetexpr(a->chord,
								 level);
				musicexpr_free(a);
				a = tmp_me;
			}
			if (bt == ME_TYPE_CHORD) {
				tmp_me = chord_to_noteoffsetexpr(b->chord,
								 level);
				musicexpr_free(b);
				b = tmp_me;
			}
			new_me = join_two_musicexprs(a, b, level);
		} else if (at == ME_TYPE_SIMULTENCE
			     || bt == ME_TYPE_SIMULTENCE) {
			/* non-simultence -> simultence and join */
			mdl_log(4,
				level,
				"turning an expression to simultence\n");
			if (at != ME_TYPE_SIMULTENCE)
				a = musicexpr_to_simultence(a, level + 1);
			if (bt != ME_TYPE_SIMULTENCE)
				b = musicexpr_to_simultence(b, level + 1);
			new_me = join_two_musicexprs(a, b, level);
		} else {
			unimplemented();
		}
	}

	assert(new_me != NULL);

	return new_me;
}

static struct musicexpr_t *
join_sequences(struct musicexpr_t *a, struct musicexpr_t *b, int level)
{
	struct tqitem_me *last_of_a, *first_of_b, *p;
	struct musicexpr_t *joined_expr;

	assert(a->me_type == ME_TYPE_SEQUENCE);
	assert(b->me_type == ME_TYPE_SEQUENCE);

	if (TAILQ_EMPTY(&a->melist)) {
		musicexpr_free(a);
		return b;
	}

	if (TAILQ_EMPTY(&b->melist)) {
		musicexpr_free(b);
		return a;
	}

	last_of_a = TAILQ_LAST(&a->melist, melist_t);
	first_of_b = TAILQ_FIRST(&b->melist);

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

	TAILQ_REMOVE(&b->melist, first_of_b, tq);
	TAILQ_REPLACE(&a->melist, last_of_a, p, tq);
	TAILQ_CONCAT(&a->melist, &b->melist, tq);

	musicexpr_free(b);

	return a;
}

static struct musicexpr_t *
join_simultences(struct musicexpr_t *a, struct musicexpr_t *b, int level)
{
	struct musicexpr_t *joined;

	unimplemented();

	return joined;
}
