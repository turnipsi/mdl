/* $Id: functions.c,v 1.6 2016/08/10 18:58:00 je Exp $ */

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
#include <err.h>
#include <limits.h>
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
		warnx("function '%s' is not defined", me->u.function.name);
		return 1;
	}
}

static int
apply_tempo(struct musicexpr *me, int level)
{
	struct musicexpr *new;
	struct funcarg *funcarg;
	const char *errstr;
	float bpm;

	assert(me->me_type == ME_TYPE_FUNCTION);

	_mdl_log(MDLLOG_FUNC, level, "applying function \"%s\"\n",
	    me->u.function.name);

	funcarg = TAILQ_FIRST(&me->u.function.args);
	if (funcarg == NULL || TAILQ_NEXT(funcarg, tq) != NULL) {
		warnx("wrong number of arguments to tempo function");
		return 1;
	}

	bpm = strtonum(funcarg->arg, 1, LLONG_MAX, &errstr);
	if (errstr != NULL) {
		warnx("invalid argument for tempo: %s", errstr);
		return 1;
	}

	new = _mdl_musicexpr_new(ME_TYPE_TEMPOCHANGE, me->id.textloc, level);
	if (new == NULL) {
		warnx("could not create a new tempo change expression");
		return 1;
	}

	new->u.tempochange.bpm = bpm;

	_mdl_musicexpr_free_subexprs(me, level);
	_mdl_musicexpr_replace(me, new, MDLLOG_FUNC, level);

	return 0;
}

static int
apply_volume(struct musicexpr *me, int level)
{
	assert(me->me_type == ME_TYPE_FUNCTION);

	_mdl_musicexpr_free_subexprs(me, level);

	/* XXX For now, just replace with an empty expression. */
	me->me_type = ME_TYPE_EMPTY;

	return 0;
}
