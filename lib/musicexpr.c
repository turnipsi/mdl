/* $Id: musicexpr.c,v 1.3 2015/11/10 20:57:47 je Exp $ */

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
#include <stdlib.h>

#include "musicexpr.h"

void
print_musicexpr(struct musicexpr_t *me)
{
	/* XXX should handle possible types in a nice way */
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
