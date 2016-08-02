/* $Id: functions.c,v 1.4 2016/08/02 20:38:19 je Exp $ */

/*
 * Copyright (c) 2016 Juha Erkkil� <je@turnipsi.no-ip.org>
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

#include "musicexpr.h"

void
_mdl_functions_apply(struct musicexpr *me, int level)
{
	struct musicexpr *p;
	struct musicexpr_iter iter;

	level += 1;

	switch (me->me_type) {
	case ME_TYPE_FUNCTION:
		/* XXX For now, just replace with an empty
		 * XXX expression.  This is not only wrong, but we leak
		 * XXX memory later. */
		 me->me_type = ME_TYPE_EMPTY;
	default:
		iter = _mdl_musicexpr_iter_new(me);
		while ((p = _mdl_musicexpr_iter_next(&iter)) != NULL)
			_mdl_functions_apply(p, level);
	}
}
