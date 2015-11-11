/* $Id: musicexpr.c,v 1.4 2015/11/11 20:02:53 je Exp $ */

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
#include <stdio.h>
#include <stdlib.h>

#include "musicexpr.h"

int
print_musicexpr(int indentlevel, struct musicexpr_t *me)
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
			ret = printf("relnote notesym=%d length=%f" \
				       " octavemods=%d\n",
				     me->relnote.notesym,
				     me->relnote.length,
				     me->relnote.octavemods);
			break;
		case ME_TYPE_SEQUENCE:
			ret = print_sequence(indentlevel + 2, me->sequence);
			break;
		case ME_TYPE_WITHOFFSET:
			ret = printf("offset_expr offset=%f\n",
				     me->offset_expr.offset);
			if (ret < 0)
				break;
			ret = print_musicexpr(indentlevel + 2,
					      me->offset_expr.me);
			break;
		default:
			assert(0);
	}

	return ret;
}

int
print_sequence(int indentlevel, struct sequence_t *seq)
{
	struct sequence_t *p;
	int ret;

	ret = 0;

	for (p = seq; p != NULL; p = p->next) {
		ret = print_musicexpr(indentlevel, p->me);
		if (ret < 0)
			break;
	}

	return ret;
}

void
free_musicexpr(struct musicexpr_t *me)
{
	switch (me->me_type) {
		case ME_TYPE_ABSNOTE:
		case ME_TYPE_RELNOTE:
			break;
		case ME_TYPE_SEQUENCE:
			free_sequence(me->sequence);
			break;
		case ME_TYPE_WITHOFFSET:
			free_musicexpr(me->offset_expr.me);
			break;
		default:
			assert(0);
	}

	free(me);
}

void
free_sequence(struct sequence_t *seq)
{
	struct sequence_t *p, *q;

	p = seq;
	while (p) {
		q = p;
		p = p->next;
		free_musicexpr(q->me);
	}
}
