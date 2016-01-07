/* $Id: joinexpr.c,v 1.18 2016/01/07 21:15:16 je Exp $ */

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

static int compare_noteoffsets(struct noteoffsetexpr_t,
			       struct noteoffsetexpr_t);

static struct musicexpr_t *
join_noteoffsetexprs(struct musicexpr_t *, struct musicexpr_t *, int); 

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

/* When this return non-NULL, it frees or reuses a and b,
 * those should never be used after passing through this function.
 * In case of error NULL is returned and a and b are left untouched. */
static struct musicexpr_t *
join_two_musicexprs(struct musicexpr_t *a, struct musicexpr_t *b, int level)
{
	struct musicexpr_t *tmp_a, *tmp_b, *tmp_me;
	enum musicexpr_type at, bt;

	if (a == NULL || b == NULL)
		return NULL;

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
				a->absnote.length += b->absnote.length;
				musicexpr_free(b);
				return a;
			}
			mdl_log(4, level + 1, "no match\n");
			break;
		case ME_TYPE_CHORD:
			mdl_log(3, level, "joining two chords\n");
			if (a->chord.chordtype == b->chord.chordtype
			      && a->chord.me->absnote.note
				   == b->chord.me->absnote.note) {
				mdl_log(4, level + 1, "matched\n");
				a->chord.me->absnote.length
				    += b->chord.me->absnote.length;
				musicexpr_free(b);
				return a;
			}
			mdl_log(4, level + 1, "no match\n");
			break;
		case ME_TYPE_NOTEOFFSETEXPR:
			mdl_log(3, level, "joining two noteoffsetexprs\n");
			tmp_me = join_noteoffsetexprs(a, b, level);
			if (tmp_me != NULL)
				return tmp_me;
			break;
		case ME_TYPE_REST:
			mdl_log(3, level, "joining two rests\n");
			a->rest.length += b->rest.length;
			musicexpr_free(b);
			return a;
		case ME_TYPE_SEQUENCE:
			mdl_log(3, level, "joining two sequences\n");
			return join_sequences(a, b, level);
		case ME_TYPE_SIMULTENCE:
			mdl_log(3, level, "joining two simultences\n");
			return join_simultences(a, b, level);
		case ME_TYPE_WITHOFFSET:
			mdl_log(3, level, "joining two exprs with offset\n");
			unimplemented();
			break;
		default:
			assert(0);
			break;
		}
	}

	/* when joined music expression types do not match
	 * or join could not be done directly */

	if (at == ME_TYPE_REST || bt == ME_TYPE_REST) {
		/* rests are incompatible with everything else */
		mdl_log(4, level, "joining rest and some --> sequence\n");
		return musicexpr_sequence(level, a, b, NULL);
	}

	if (at == ME_TYPE_SEQUENCE || bt == ME_TYPE_SEQUENCE) {
		/* non-sequence --> wrap to sequence and join sequences */
		mdl_log(4, level, "wrapping an expression to sequence\n");
		tmp_a = (at != ME_TYPE_SEQUENCE)
			  ? musicexpr_sequence(level, a, NULL)
			  : a;
		tmp_b = (bt != ME_TYPE_SEQUENCE)
			  ? musicexpr_sequence(level, b, NULL)
			  : b;
		tmp_me = join_two_musicexprs(tmp_a, tmp_b, level);
		if (tmp_me == NULL) {
			if (tmp_a != a)
				free_melist(tmp_a);
			if (tmp_b != b)
				free_melist(tmp_b);
		}
		return tmp_me;
	}

	if (at == ME_TYPE_CHORD || bt == ME_TYPE_CHORD) {
		/* chord --> noteoffsetexpr and join */
		mdl_log(4, level, "converting chord to noteoffsetexpr\n");
		tmp_a = (at == ME_TYPE_CHORD)
			  ? chord_to_noteoffsetexpr(a->chord, level)
			  : a;
		tmp_b = (bt == ME_TYPE_CHORD)
			  ? chord_to_noteoffsetexpr(b->chord, level)
			  : b;
	} else if (at == ME_TYPE_SIMULTENCE || bt == ME_TYPE_SIMULTENCE) {
		/* non-simultence -> simultence and join */
		mdl_log(4, level, "converting an expression to simultence\n");
		tmp_a = (at != ME_TYPE_SIMULTENCE)
			  ? musicexpr_to_simultence(a, level + 1)
			  : a;
		tmp_b = (bt != ME_TYPE_SIMULTENCE)
			  ? musicexpr_to_simultence(b, level + 1)
			  : b;
	} else {
		assert(0);
	}

	tmp_me = join_two_musicexprs(tmp_a, tmp_b, level);

	if (tmp_me != NULL) {
		/* joining succeeded, if using converted expressions
		 * then free the original expressions before returning
		 * the joined expression */
		if (tmp_a != a)
			musicexpr_free(a);
		if (tmp_b != b)
			musicexpr_free(b);
	} else {
		/* joining failed, if using converted expressions
		 * free the temporary expressions before returning NULL */
		if (tmp_a != a)
			musicexpr_free(tmp_a);
		if (tmp_b != b)
			musicexpr_free(tmp_b);
	}

	return tmp_me;
}

static struct musicexpr_t *
join_noteoffsetexprs(struct musicexpr_t *a, struct musicexpr_t *b, int level)
{
	struct musicexpr_t *joined_subexpr, *tmp_a, *tmp_b;

	if (compare_noteoffsets(a->noteoffsetexpr, b->noteoffsetexpr) != 0) {
		mdl_log(4,
			level + 1,
			"could not join noteoffexprs directly\n");
		return NULL;
	}

	if ((tmp_a = musicexpr_clone(a->noteoffsetexpr.me, level)) == NULL)
		return NULL;

	if ((tmp_b = musicexpr_clone(b->noteoffsetexpr.me, level)) == NULL) {
		musicexpr_free(tmp_a);
		return NULL;
	}

	joined_subexpr = join_two_musicexprs(tmp_a, tmp_b, level);
	if (joined_subexpr == NULL) {
		musicexpr_free(tmp_a);
		musicexpr_free(tmp_b);
		return NULL;
	}

	a->noteoffsetexpr.me = joined_subexpr;
	musicexpr_free(b);

	return joined_subexpr;
}

static int
compare_noteoffsets(struct noteoffsetexpr_t a, struct noteoffsetexpr_t b)
{
	int min_i, i;

	min_i = (a.count < b.count) ? a.count : b.count;
			       
	for (i = 0; i < min_i; i++) {
		if (a.offsets[i] < b.offsets[i]) {
			return -1;
		} else if (a.offsets[i] > b.offsets[i]) {
			return 1;
		}
	}

	if (a.count < b.count) {
		return -1;
	} else if (a.count > b.count) {
		return 1;
	}
 
	return 0;       
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

	if ((joined_expr = malloc(sizeof(struct musicexpr_t))) == NULL) {
		warn("malloc in join_sequences");
		return NULL;
	}
	if ((p = malloc(sizeof(struct tqitem_me))) == NULL) {
		warn("malloc in join_sequences");
		free(joined_expr);
		return NULL;
	}

	p->me = joined_expr;

	last_of_a = TAILQ_LAST(&a->melist, melist_t);
	first_of_b = TAILQ_FIRST(&b->melist);

	joined_expr->me_type = ME_TYPE_JOINEXPR;
	joined_expr->joinexpr.a = last_of_a->me;
	joined_expr->joinexpr.b = first_of_b->me;

	if (joinexpr_musicexpr(joined_expr, level + 1) != 0) {
		free(joined_expr);
		free(p);
		return NULL;
	}

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
