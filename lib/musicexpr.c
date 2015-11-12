/* $Id: musicexpr.c,v 1.7 2015/11/12 20:26:57 je Exp $ */

/*
 * Copyright (c) 2015 Juha Erkkil� <je@turnipsi.no-ip.org>
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

#include "midi.h"
#include "musicexpr.h"

int
musicexpr_offsetize(struct musicexpr_t *me)
{
	/* XXX */

	return 0;
}

int
musicexpr_relative_to_absolute(struct musicexpr_t *me)
{
	/* XXX */

	return 0;
}

int
musicexpr_to_midievents(struct musicexpr_t *me)
{
	/* XXX */

	return 0;
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
			ret = printf("relnote notesym=%d length=%f" \
				       " octavemods=%d\n",
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
			ret = musicexpr_print(indentlevel + 2,
					      me->offset_expr.me);
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
