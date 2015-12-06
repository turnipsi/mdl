/* $Id: joinexpr.c,v 1.1 2015/12/06 20:41:48 je Exp $ */

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

#include "joinexpr.h"
#include "musicexpr.h"

static void	join_exprs_in_sequence(struct sequence_t *, int);
static void	join_expressions(struct musicexpr_t *,
				 struct musicexpr_t *,
				 int);

void
joinexpr_musicexpr(struct musicexpr_t *me, int level)
{
	/* XXX */

	switch (me->me_type) {
	case ME_TYPE_ABSNOTE:
	case ME_TYPE_JOINEXPR:
	case ME_TYPE_RELNOTE:
	case ME_TYPE_REST:
		break;
	case ME_TYPE_SEQUENCE:
		join_exprs_in_sequence(me->sequence, level + 1);
		break;
	case ME_TYPE_WITHOFFSET:
		joinexpr_musicexpr(me->offset_expr.me, level + 1);
		break;
	default:
		assert(0);
	}
}

static void
join_exprs_in_sequence(struct sequence_t *seq, int level)
{
	/* XXX call join_expressions appropriately
	 * XXX this function handles the binary operator ~
	 * XXX and calls join_expressions() */
}

static void
join_expressions(struct musicexpr_t *a, struct musicexpr_t *b, int level)
{
	/* this function evaluates joins */
	assert(a->me_type != ME_TYPE_JOINEXPR);
	assert(b->me_type != ME_TYPE_JOINEXPR);

	/* relative notes can not be joined
	 * (think of case "cis ~ des g"... if "des" disappears then
	 * (absolute note) "g" gets resolved differently) */
	assert(a->me_type != ME_TYPE_RELNOTE);
	assert(b->me_type != ME_TYPE_RELNOTE);

	switch (a->me_type) {
	case ME_TYPE_ABSNOTE:
		switch (b->me_type) {
		case ME_TYPE_ABSNOTE:
			break;
		case ME_TYPE_REST:
			break;
		case ME_TYPE_SEQUENCE:
			break;
		case ME_TYPE_WITHOFFSET:
			break;
		default:
			assert(0);
		}
		break;
	case ME_TYPE_REST:
		switch (b->me_type) {
		case ME_TYPE_ABSNOTE:
			break;
		case ME_TYPE_REST:
			break;
		case ME_TYPE_SEQUENCE:
			break;
		case ME_TYPE_WITHOFFSET:
			break;
		default:
			assert(0);
		}
		break;
	case ME_TYPE_SEQUENCE:
		switch (b->me_type) {
		case ME_TYPE_ABSNOTE:
			break;
		case ME_TYPE_REST:
			break;
		case ME_TYPE_SEQUENCE:
			break;
		case ME_TYPE_WITHOFFSET:
			break;
		default:
			assert(0);
		}
		break;
	case ME_TYPE_WITHOFFSET:
		switch (b->me_type) {
		case ME_TYPE_ABSNOTE:
			break;
		case ME_TYPE_REST:
			break;
		case ME_TYPE_SEQUENCE:
			break;
		case ME_TYPE_WITHOFFSET:
			break;
		default:
			assert(0);
		}
		break;
	default:
		assert(0);
	}
}
