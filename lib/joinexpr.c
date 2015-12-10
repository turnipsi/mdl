/* $Id: joinexpr.c,v 1.3 2015/12/10 19:40:04 je Exp $ */

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
#include <stdlib.h>

#include "joinexpr.h"
#include "musicexpr.h"

static int	join_joinexpr(struct musicexpr_t *, int);
static int	make_sequence_of_two(struct musicexpr_t *,
				     struct musicexpr_t *,
				     struct musicexpr_t *);

int
joinexpr_musicexpr(struct musicexpr_t *me, int level)
{
	struct sequence_t *seq;
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
		for (seq = me->sequence; seq != NULL; seq = seq->next)
			ret = joinexpr_musicexpr(seq->me, level + 1);
			if (ret != 0)
				break;
		break;
	case ME_TYPE_WITHOFFSET:
		ret = joinexpr_musicexpr(me->offset_expr.me, level + 1);
		break;
	default:
		assert(0);
	}

	return ret;
}

static int
join_joinexpr(struct musicexpr_t *me, int level)
{
	struct musicexpr_t *a, *b;
	int ret;

	assert(me->me_type == ME_TYPE_JOINEXPR);

	ret = 0;

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

	/* XXX take the first or last of sequences and do joining to those */
	/* XXX withoffset gets joined only if offset == 0 ??? */

	switch (a->me_type) {
	case ME_TYPE_ABSNOTE:
		switch (b->me_type) {
		case ME_TYPE_ABSNOTE:
			if (a->absnote.note == b->absnote.note) {
				a->absnote.length += b->absnote.length;
				musicexpr_free(b);
				me->me_type = ME_TYPE_ABSNOTE;
				me->absnote = a->absnote;
			} else {
				ret = make_sequence_of_two(me, a, b);
			}
			break;
		case ME_TYPE_REST:
			ret = make_sequence_of_two(me, a, b);
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
	case ME_TYPE_REST:
		switch (b->me_type) {
		case ME_TYPE_ABSNOTE:
			ret = make_sequence_of_two(me, a, b);
			break;
		case ME_TYPE_REST:
			a->rest.length += b->rest.length;
			musicexpr_free(b);
			me->me_type = ME_TYPE_REST;
			me->rest = a->rest;
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
	case ME_TYPE_SEQUENCE:
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

	return ret;
}

static int
make_sequence_of_two(struct musicexpr_t *me,
		     struct musicexpr_t *a,
		     struct musicexpr_t *b)
{
	struct sequence_t *p, *q;

	if ((p = malloc(sizeof(struct sequence_t))) == NULL) {
		warnx("malloc in make_sequence_of_two");
		return 1;
	}
	if ((q = malloc(sizeof(struct sequence_t))) == NULL) {
		warnx("malloc in make_sequence_of_two");
		free(p);
		return 1;
	}

	p->me = a;
	p->next = q;
	q->me = b;
	q->next = NULL;

	me->me_type = ME_TYPE_SEQUENCE;
	me->sequence = p;

	return 0;
}
