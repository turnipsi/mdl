/* $Id: functions.c,v 1.2 2016/07/31 17:18:40 je Exp $ */

/*
 * Copyright (c) 2016 Juha Erkkilä <je@turnipsi.no-ip.org>
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

#include "musicexpr.h"

void
_mdl_functions_apply(struct musicexpr *me, int level)
{
	struct musicexpr *p;

	/*
	 * XXX There is probably a common pattern here: do some operation
	 * XXX to all subexpressions... but the knowledge "what are the
	 * XXX subexpressions" is not anywhere.
	 */

	level += 1;
	
	switch (me->me_type) {
	case ME_TYPE_ABSDRUM:
	case ME_TYPE_ABSNOTE:
	case ME_TYPE_EMPTY:
	case ME_TYPE_RELDRUM:
	case ME_TYPE_RELNOTE:
	case ME_TYPE_REST:
		/* Nothing to do. */
		break;
	case ME_TYPE_CHORD:
		_mdl_functions_apply(me->u.chord.me, level);
		break;
	case ME_TYPE_FLATSIMULTENCE:
		_mdl_functions_apply(me->u.flatsimultence.me, level);
		break;
	case ME_TYPE_FUNCTION:
		/* XXX For now, just replace with an empty expression: */
		me->me_type = ME_TYPE_EMPTY;
		break;
	case ME_TYPE_JOINEXPR:
		_mdl_functions_apply(me->u.joinexpr.a, level);
		_mdl_functions_apply(me->u.joinexpr.b, level);
		break;
	case ME_TYPE_NOTEOFFSETEXPR:
		_mdl_functions_apply(me->u.noteoffsetexpr.me, level);
		break;
	case ME_TYPE_OFFSETEXPR:
		_mdl_functions_apply(me->u.offsetexpr.me, level);
		break;
	case ME_TYPE_ONTRACK:
		_mdl_functions_apply(me->u.ontrack.me, level);
		break;
	case ME_TYPE_RELSIMULTENCE:
	case ME_TYPE_SCALEDEXPR:
		_mdl_functions_apply(me->u.scaledexpr.me, level);
		break;
	case ME_TYPE_SEQUENCE:
	case ME_TYPE_SIMULTENCE:
		TAILQ_FOREACH(p, &me->u.melist, tq)
			_mdl_functions_apply(p, level);
		break;
	default:
		assert(0);
	}
}
