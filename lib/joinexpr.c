/* $Id: joinexpr.c,v 1.45 2016/03/25 20:48:36 je Exp $ */

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
#include <math.h>
#include <stdlib.h>

#include "joinexpr.h"
#include "musicexpr.h"

static struct musicexpr *join_two_musicexprs(struct musicexpr *,
    struct musicexpr *, int);
static int compare_noteoffsets(struct noteoffsetexpr, struct noteoffsetexpr);
static struct musicexpr *join_noteoffsetexprs(struct musicexpr *,
    struct musicexpr *, int);
static struct musicexpr *join_sequences(struct musicexpr *,
    struct musicexpr *, int);
static struct musicexpr *join_flat_simultences(struct musicexpr *,
    struct musicexpr *, int);

int
joinexpr_musicexpr(struct musicexpr *me, int level)
{
	struct musicexpr *joined_me, *p;
	char *me_id;
	int ret;

	ret = 0;

	assert(me->me_type != ME_TYPE_RELNOTE);
	assert(me->me_type != ME_TYPE_RELSIMULTENCE);

	if ((me_id = musicexpr_id_string(me)) != NULL) {
		mdl_log(MDLLOG_JOINS, level,
		    "joining possible subexpressions in %s\n", me_id);
		free(me_id);
	}

	switch (me->me_type) {
	case ME_TYPE_ABSNOTE:
	case ME_TYPE_EMPTY:
	case ME_TYPE_REST:
		break;
	case ME_TYPE_CHORD:
		/*
		 * The possible subexpressions of chords are such that
		 * calling joinexpr_musicexpr() is a no-op.
		 */
		assert(me->u.chord.me->me_type == ME_TYPE_ABSNOTE ||
		    me->u.chord.me->me_type == ME_TYPE_RELNOTE);
		break;
	case ME_TYPE_JOINEXPR:
		ret = joinexpr_musicexpr(me->u.joinexpr.a, (level + 1));
		if (ret != 0)
			break;
		ret = joinexpr_musicexpr(me->u.joinexpr.b, (level + 1));
		if (ret != 0)
			break;

		joined_me = join_two_musicexprs(me->u.joinexpr.a,
		    me->u.joinexpr.b, level);
		if (joined_me == NULL) {
			ret = 1;
			break;
		}
		musicexpr_copy(me, joined_me);
		break;
	case ME_TYPE_OFFSETEXPR:
		ret = joinexpr_musicexpr(me->u.offsetexpr.me, (level + 1));
		break;
	case ME_TYPE_ONTRACK:
		ret = joinexpr_musicexpr(me->u.ontrack.me, (level + 1));
		break;
	case ME_TYPE_SCALEDEXPR:
		ret = joinexpr_musicexpr(me->u.scaledexpr.me, (level + 1));
		break;
	case ME_TYPE_SEQUENCE:
	case ME_TYPE_SIMULTENCE:
		TAILQ_FOREACH(p, &me->u.melist, tq) {
			ret = joinexpr_musicexpr(p, (level + 1));
			if (ret != 0)
				break;
		}
		break;
	default:
		assert(0);
	}

	return ret;
}

/*
 * When this return non-NULL, it frees or reuses a and b,
 * those should never be used after passing through this function.
 * In case of error NULL is returned and a and b are left untouched.
 */
static struct musicexpr *
join_two_musicexprs(struct musicexpr *a, struct musicexpr *b, int level)
{
	struct musicexpr *tmp_a, *tmp_b, *tmp_me;
	enum musicexpr_type at, bt;
	char *a_id, *b_id;

	if (a == NULL || b == NULL)
		return NULL;

	if ((a_id = musicexpr_id_string(a)) != NULL) {
		if ((b_id = musicexpr_id_string(b)) != NULL) {
			mdl_log(MDLLOG_JOINS, level,
			    "joining expressions %s and %s\n",
			    a_id, b_id);
			free(b_id);
		}
		free(a_id);
	}

	level += 1;

	at = a->me_type;
	bt = b->me_type;

	/* We should have handled the subexpressions before entering here. */
	assert(at != ME_TYPE_JOINEXPR);
	assert(bt != ME_TYPE_JOINEXPR);

	/*
	 * Relative notes can not be joined (think of case "cis ~ des g"...
	 * if "des" disappears then (absolute note) "g" gets resolved
	 * differently).  Thus it is callers responsibility to call
	 * musicexpr_relative_to_absolute() for both a and b before calling
	 * this.
	 */
	assert(at != ME_TYPE_RELNOTE);
	assert(bt != ME_TYPE_RELNOTE);
	assert(at != ME_TYPE_RELSIMULTENCE);
	assert(bt != ME_TYPE_RELSIMULTENCE);

	if (at == ME_TYPE_CHORD)
		assert(a->u.chord.me->me_type == ME_TYPE_ABSNOTE);
	if (bt == ME_TYPE_CHORD)
		assert(b->u.chord.me->me_type == ME_TYPE_ABSNOTE);

	if (at == bt) {
		/* Handle cases where joined music expression types match. */
		switch (at) {
		case ME_TYPE_ABSNOTE:
			/*
			 * XXX This should also take into account the track
			 * XXX parameter and possible instrument changes.
			 */
			if (a->u.absnote.note == b->u.absnote.note) {
				mdl_log(MDLLOG_JOINS, level,
				    "matched absnotes\n");
				a->u.absnote.length += b->u.absnote.length;
				musicexpr_free(b, level);
				return a;
			}
			mdl_log(MDLLOG_JOINS, level,
			    "absnotes do not match\n");
			break;
		case ME_TYPE_CHORD:
			if (a->u.chord.chordtype == b->u.chord.chordtype &&
			    a->u.chord.me->u.absnote.note ==
			    b->u.chord.me->u.absnote.note) {
				mdl_log(MDLLOG_JOINS, level,
				    "matched chords\n");
				a->u.chord.me->u.absnote.length +=
				    b->u.chord.me->u.absnote.length;
				musicexpr_free(b, level);
				return a;
			}
			mdl_log(MDLLOG_JOINS, level, "chords do not match\n");
			break;
		case ME_TYPE_EMPTY:
			mdl_log(MDLLOG_JOINS, level, "joining two nulls\n");
			musicexpr_free(b, level);
			return a;
		case ME_TYPE_NOTEOFFSETEXPR:
			mdl_log(MDLLOG_JOINS, level,
			    "joining two noteoffsetexprs\n");
			tmp_me = join_noteoffsetexprs(a, b, level);
			if (tmp_me != NULL)
				return tmp_me;
			break;
		case ME_TYPE_OFFSETEXPR:
			mdl_log(MDLLOG_JOINS, level,
			    "joining two exprs with offset\n");
			unimplemented();
			break;
		case ME_TYPE_ONTRACK:
			/* fallthrough to indirect joins */
			break;
		case ME_TYPE_REST:
			mdl_log(MDLLOG_JOINS, level, "joining two rests\n");
			a->u.rest.length += b->u.rest.length;
			musicexpr_free(b, level);
			return a;
		case ME_TYPE_SCALEDEXPR:
			mdl_log(MDLLOG_JOINS, level,
			    "joining two scaled expressions\n");
			/* fallthrough to indirect joins */
			break;
		case ME_TYPE_SEQUENCE:
			mdl_log(MDLLOG_JOINS, level,
			    "joining two sequences\n");
			return join_sequences(a, b, level);
		case ME_TYPE_SIMULTENCE:
			mdl_log(MDLLOG_JOINS, level,
			    "joining two simultences\n");
			/* fallthrough to indirect joins */
			break;
		default:
			assert(0);
			break;
		}
	}

	/*
	 * Handle cases where joined music expression types do not match
	 * or join could not be done directly.
	 */

	if (at == ME_TYPE_REST || bt == ME_TYPE_REST) {
		/* Rests are incompatible with everything else. */
		mdl_log(MDLLOG_JOINS, level,
		    "joining rest and some --> sequence\n");
		return musicexpr_sequence(level, a, b, NULL);
	}

	if (at == ME_TYPE_ONTRACK || bt == ME_TYPE_ONTRACK) {
		/*
		 * Refuse to join expressions where one or both are track
		 * expressions.  Some of those could be joined, but perhaps
		 * we do not want to deal with that.  Syntax error might be
		 * in place if this is tried?
		 */
		mdl_log(MDLLOG_JOINS, level,
		    "refusing to join expressions where one expression is a"
		    " track expression\n");
		return musicexpr_sequence(level, a, b, NULL);
	}

	if (at == ME_TYPE_SEQUENCE || bt == ME_TYPE_SEQUENCE) {
		/* Non-sequence --> wrap to sequence and join sequences. */
		mdl_log(MDLLOG_JOINS, level,
		    "wrapping an expression to sequence\n");
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
		/* Chord --> noteoffsetexpr and join. */
		mdl_log(MDLLOG_JOINS, level,
		    "converting chord to noteoffsetexpr\n");
		tmp_a = (at == ME_TYPE_CHORD)
			    ? chord_to_noteoffsetexpr(a->u.chord, level)
			    : a;
		tmp_b = (bt == ME_TYPE_CHORD)
			    ? chord_to_noteoffsetexpr(b->u.chord, level)
			    : b;
	} else {
		/* anything -> flat simultence and join. */
		mdl_log(MDLLOG_JOINS, level,
		    "converting expressions to flat simultence\n");
		tmp_a = musicexpr_to_flat_simultence(a, (level + 1));
		tmp_b = musicexpr_to_flat_simultence(b, (level + 1));
	}

	if (tmp_a->me_type == ME_TYPE_FLATSIMULTENCE &&
	    tmp_b->me_type == ME_TYPE_FLATSIMULTENCE) {
		mdl_log(MDLLOG_JOINS, level, "joining two flat simultences\n");
		tmp_me = join_flat_simultences(tmp_a, tmp_b, (level + 1));
	} else {
		tmp_me = join_two_musicexprs(tmp_a, tmp_b, level);
	}

	if (tmp_me != NULL) {
		/*
		 * Joining succeeded.  If using converted expressions
		 * then free the original expressions before returning
		 * the joined expression.
		 */
		if (tmp_a != a)
			musicexpr_free(a, level);
		if (tmp_b != b)
			musicexpr_free(b, level);
	} else {
		/*
		 * Joining failed.  If using converted expressions
		 * free the temporary expressions before returning NULL.
		 */
		if (tmp_a != a)
			musicexpr_free(tmp_a, level);
		if (tmp_b != b)
			musicexpr_free(tmp_b, level);
	}

	return tmp_me;
}

static struct musicexpr *
join_noteoffsetexprs(struct musicexpr *a, struct musicexpr *b, int level)
{
	struct musicexpr *joined_subexpr, *tmp_a, *tmp_b;
	int ret;

	ret = compare_noteoffsets(a->u.noteoffsetexpr, b->u.noteoffsetexpr);
	if (ret != 0) {
		mdl_log(MDLLOG_JOINS, (level + 1),
		    "could not join noteoffexprs directly\n");
		return NULL;
	}

	tmp_a = musicexpr_clone(a->u.noteoffsetexpr.me, level);
	if (tmp_a == NULL)
		return NULL;

	tmp_b = musicexpr_clone(b->u.noteoffsetexpr.me, level);
	if (tmp_b == NULL) {
		musicexpr_free(tmp_a, level);
		return NULL;
	}

	joined_subexpr = join_two_musicexprs(tmp_a, tmp_b, level);
	if (joined_subexpr == NULL) {
		musicexpr_free(tmp_a, level);
		musicexpr_free(tmp_b, level);
		return NULL;
	}

	a->u.noteoffsetexpr.me = joined_subexpr;
	musicexpr_free(b, level);

	return joined_subexpr;
}

static int
compare_noteoffsets(struct noteoffsetexpr a, struct noteoffsetexpr b)
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

static struct musicexpr *
join_sequences(struct musicexpr *a, struct musicexpr *b, int level)
{
	struct musicexpr *joined_expr, *last_of_a, *first_of_b;

	assert(a->me_type == ME_TYPE_SEQUENCE);
	assert(b->me_type == ME_TYPE_SEQUENCE);

	if (TAILQ_EMPTY(&a->u.melist)) {
		musicexpr_free(a, level);
		return b;
	}
	if (TAILQ_EMPTY(&b->u.melist)) {
		musicexpr_free(b, level);
		return a;
	}

	joined_expr = musicexpr_new(ME_TYPE_JOINEXPR, textloc_zero(),
	    (level + 1));
	if (joined_expr == NULL)
		return NULL;

	last_of_a = TAILQ_LAST(&a->u.melist, melist);
	first_of_b = TAILQ_FIRST(&b->u.melist);

	joined_expr->u.joinexpr.a = last_of_a;
	joined_expr->u.joinexpr.b = first_of_b;

	if (joinexpr_musicexpr(joined_expr, (level + 1)) != 0) {
		free(joined_expr);
		return NULL;
	}

	TAILQ_REMOVE(&b->u.melist, first_of_b, tq);
	TAILQ_REPLACE(&a->u.melist, last_of_a, joined_expr, tq);
	TAILQ_CONCAT(&a->u.melist, &b->u.melist, tq);

	musicexpr_free(b, level);

	return a;
}

static struct musicexpr *
join_flat_simultences(struct musicexpr *a, struct musicexpr *b, int level)
{
	struct musicexpr *p, *q, *r, *sim_a, *sim_b;
	struct offsetexpr *p_me, *q_me;
	float prev_note_end, next_note_start;

	assert(a->me_type == ME_TYPE_FLATSIMULTENCE);
	assert(b->me_type == ME_TYPE_FLATSIMULTENCE);

	sim_a = a->u.flatsimultence.me;
	sim_b = b->u.flatsimultence.me;

	assert(sim_a->me_type == ME_TYPE_SIMULTENCE);
	assert(sim_b->me_type == ME_TYPE_SIMULTENCE);

	/*
	 * Join notes when we can.  This is inefficient with simultences
	 * with lots of notes, but hopefully is not misused much.
	 */
	TAILQ_FOREACH(p, &sim_a->u.melist, tq) {
		assert(p->me_type == ME_TYPE_OFFSETEXPR);
		p_me = &p->u.offsetexpr;

		assert(p_me->me->me_type == ME_TYPE_ABSNOTE);

		prev_note_end = p_me->offset + p_me->me->u.absnote.length;

		TAILQ_FOREACH_SAFE(q, &sim_b->u.melist, tq, r) {
			assert(q->me_type == ME_TYPE_OFFSETEXPR);
			q_me = &q->u.offsetexpr;

			assert(q_me->me->me_type == ME_TYPE_ABSNOTE);

			if (p_me->me->u.absnote.note !=
			    q_me->me->u.absnote.note)
				continue;

			/*
			 * XXX This should also take into account the track
			 * XXX parameter and possible instrument changes.
			 */

			/*
			 * This is somewhat curious way of joining, because
			 * to join to music expression "a" q_me->offset
			 * must be zero or negative.
			 */
			next_note_start = a->u.flatsimultence.length
			    + q_me->offset;

			if (fabs(prev_note_end - next_note_start) >=
			    MINIMUM_MUSICEXPR_LENGTH)
				continue;

			mdl_log(MDLLOG_JOINS, level,
			    "joining absnote expressions in simultence\n");

			p_me->me->u.absnote.length +=
			    q_me->me->u.absnote.length;

			TAILQ_REMOVE(&sim_b->u.melist, q, tq);
			musicexpr_free(q, level);
		}
	}

	TAILQ_FOREACH(q, &sim_b->u.melist, tq)
		q->u.offsetexpr.offset += a->u.flatsimultence.length;

	TAILQ_CONCAT(&sim_a->u.melist, &sim_b->u.melist, tq);

	a->u.flatsimultence.length += b->u.flatsimultence.length;

	musicexpr_free(b, level);

	return a;
}
