/* $Id: functions.c,v 1.5 2016/08/08 08:47:33 je Exp $ */

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

#include <sys/queue.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "musicexpr.h"

static int	apply_function(struct musicexpr *, int);
static int	apply_tempo(struct musicexpr *, int);
static int	apply_volume(struct musicexpr *, int);

int
_mdl_functions_apply(struct musicexpr *me, int level)
{
	struct musicexpr *p;
	struct musicexpr_iter iter;
	int ret;

	level += 1;

	if (me->me_type == ME_TYPE_FUNCTION)
		return apply_function(me, level);

	iter = _mdl_musicexpr_iter_new(me);
	while ((p = _mdl_musicexpr_iter_next(&iter)) != NULL) {
		if ((ret = _mdl_functions_apply(p, level)) != 0)
			return ret;
	}

	return 0;
}

void
_mdl_functions_free(struct musicexpr *me)
{
	struct function *func;
	struct funcarg *p, *q;

	assert(me->me_type == ME_TYPE_FUNCTION);

	func = &me->u.function;

	TAILQ_FOREACH_SAFE(p, &func->args, tq, q) {
		TAILQ_REMOVE(&func->args, p, tq);
		free(p);
	}
	free(func->name);
}

static int
apply_function(struct musicexpr *me, int level)
{
	assert(me->me_type == ME_TYPE_FUNCTION);

	if (strcmp(me->u.function.name, "tempo") == 0) {
		return apply_tempo(me, level);
	} else if (strcmp(me->u.function.name, "volume") == 0) {
		return apply_volume(me, level);
	} else {
		return 1;
	}
}

static int
apply_tempo(struct musicexpr *me, int level)
{
	_mdl_musicexpr_free_subexprs(me, level);

	/* XXX For now, just replace with an empty expression. */
	me->me_type = ME_TYPE_EMPTY;

	return 0;
}

static int
apply_volume(struct musicexpr *me, int level)
{
	_mdl_musicexpr_free_subexprs(me, level);

	/* XXX For now, just replace with an empty expression. */
	me->me_type = ME_TYPE_EMPTY;

	return 0;
}
