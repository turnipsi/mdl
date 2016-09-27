/* $Id: musicexpr.c,v 1.132 2016/09/27 13:10:01 je Exp $ */

/*
 * Copyright (c) 2015, 2016 Juha Erkkilä <je@turnipsi.no-ip.org>
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
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "musicexpr.h"
#include "util.h"

int musicexpr_id_counter = 0;

static struct musicexpr	*musicexpr_tq(enum musicexpr_type me_type,
    int, struct musicexpr *, va_list va);
static struct musicexpr	*musicexpr_simultence(int, struct musicexpr *, ...);
static struct musicexpr	*musicexpr_scale_in_time(struct musicexpr *, float,
    int);

static int	_mdl_musicexpr_clone_melist(struct melist *, struct melist,
    int);
static void	_mdl_musicexpr_log_chordtype(enum chordtype, enum logtype, int,
    char *);
static void	_mdl_musicexpr_log_melist(struct melist, enum logtype, int,
    char *);
static void	_mdl_musicexpr_apply_noteoffset(struct musicexpr *, int, int);
static void	_mdl_musicexpr_stretch_length(struct musicexpr *,
    float);

static int	add_as_offsetexpr_to_flat_simultence(struct musicexpr *,
    struct musicexpr *, float *, int);
static int	add_musicexpr_to_flat_simultence(struct musicexpr *,
    struct musicexpr *, float *, int);
static float	musicexpr_calc_length(struct musicexpr *);

static void	tag_as_joining(struct musicexpr *, int);

struct musicexpr *
_mdl_musicexpr_clone(struct musicexpr *me, int level)
{
	struct musicexpr *cloned;
	int ret;
	char *me_id1, *me_id2;

	cloned = _mdl_musicexpr_new(me->me_type, me->id.textloc, level+1);
	if (cloned == NULL)
		return NULL;

	level += 1;

	cloned->joining = me->joining;
	cloned->u = me->u;

	/* XXX could use subexpression iterators? */
	switch (me->me_type) {
	case ME_TYPE_CHORD:
		cloned->u.chord.me = _mdl_musicexpr_clone(me->u.chord.me,
		    level);
		if (cloned->u.chord.me == NULL) {
			free(cloned);
			cloned = NULL;
		}
		break;
	case ME_TYPE_FLATSIMULTENCE:
		cloned->u.flatsimultence.me =
		    _mdl_musicexpr_clone(me->u.flatsimultence.me, level);
		if (cloned->u.flatsimultence.me == NULL) {
			free(cloned);
			cloned = NULL;
		}
		break;
	case ME_TYPE_FUNCTION:
		/* XXX Functions must have been handled before cloning. */
		assert(0);
		break;
	case ME_TYPE_JOINEXPR:
		cloned->u.joinexpr.a = _mdl_musicexpr_clone(me->u.joinexpr.a,
		    level);
		if (cloned->u.joinexpr.a == NULL) {
			free(cloned);
			cloned = NULL;
			break;
		}
		cloned->u.joinexpr.b = _mdl_musicexpr_clone(me->u.joinexpr.b,
		    level);
		if (cloned->u.joinexpr.b == NULL) {
			_mdl_musicexpr_free(cloned->u.joinexpr.a, level+1);
			free(cloned);
			cloned = NULL;
		}
		break;
	case ME_TYPE_NOTEOFFSETEXPR:
		cloned->u.noteoffsetexpr.me =
		    _mdl_musicexpr_clone(me->u.noteoffsetexpr.me, level);
		if (cloned->u.noteoffsetexpr.me == NULL) {
			free(cloned);
			cloned = NULL;
			break;
		}
		cloned->u.noteoffsetexpr.offsets =
		    malloc(me->u.noteoffsetexpr.count);
		if (cloned->u.noteoffsetexpr.offsets == NULL) {
			_mdl_musicexpr_free(cloned->u.noteoffsetexpr.me,
			    level+1);
			free(cloned);
			cloned = NULL;
			break;
		}
		memcpy(cloned->u.noteoffsetexpr.offsets,
		    me->u.noteoffsetexpr.offsets,
		    me->u.noteoffsetexpr.count * sizeof(int));
		break;
	case ME_TYPE_OFFSETEXPR:
		cloned->u.offsetexpr.me =
		    _mdl_musicexpr_clone(me->u.offsetexpr.me, level);
		if (cloned->u.offsetexpr.me == NULL) {
			free(cloned);
			cloned = NULL;
		}
		break;
	case ME_TYPE_ONTRACK:
		cloned->u.ontrack.me = _mdl_musicexpr_clone(me->u.ontrack.me,
		    level);
		if (cloned->u.ontrack.me == NULL) {
			free(cloned);
			cloned = NULL;
			break;
		}
		break;
	case ME_TYPE_RELSIMULTENCE:
	case ME_TYPE_SCALEDEXPR:
		cloned->u.scaledexpr.me =
		    _mdl_musicexpr_clone(me->u.scaledexpr.me, level);
		if (cloned->u.scaledexpr.me == NULL) {
			free(cloned);
			cloned = NULL;
		}
		break;
	case ME_TYPE_SEQUENCE:
	case ME_TYPE_SIMULTENCE:
		ret = _mdl_musicexpr_clone_melist(&cloned->u.melist,
		    me->u.melist, level);
		if (ret != 0) {
			free(cloned);
			cloned = NULL;
		}
		break;
	default:
		;
	}

	if ((me_id1 = _mdl_musicexpr_id_string(me)) != NULL) {
		if ((me_id2 = _mdl_musicexpr_id_string(cloned)) != NULL) {
			_mdl_log(MDLLOG_MM, level, "cloning %s as %s\n",
			    me_id1, me_id2);
			free(me_id2);
		}
		free(me_id1);
	}

	return cloned;
}

static int
_mdl_musicexpr_clone_melist(struct melist *cloned_melist, struct melist melist,
    int level)
{
	struct musicexpr *p, *q;

	TAILQ_INIT(cloned_melist);

	TAILQ_FOREACH(p, &melist, tq) {
		q = _mdl_musicexpr_clone(p, level);
		if (q == NULL) {
			warnx("cloud not clone music expression list");
			_mdl_musicexpr_free_melist(*cloned_melist, level+1);
			return 1;
		}
		TAILQ_INSERT_TAIL(cloned_melist, q, tq);
	}

	return 0;
}

static struct musicexpr *
musicexpr_tq(enum musicexpr_type me_type, int level,
    struct musicexpr *listitem, va_list va)
{
	struct musicexpr *me;

	assert(me_type == ME_TYPE_SEQUENCE || me_type == ME_TYPE_SIMULTENCE);

	me = _mdl_musicexpr_new(me_type, _mdl_textloc_zero(), level);
	if (me == NULL)
		return NULL;

	TAILQ_INIT(&me->u.melist);

	while (listitem != NULL) {
		TAILQ_INSERT_TAIL(&me->u.melist, listitem, tq);
		listitem = va_arg(va, struct musicexpr *);
	}

	va_end(va);

	return me;
}

struct musicexpr *
_mdl_musicexpr_sequence(int level, struct musicexpr *next_me, ...)
{
	va_list va;
	struct musicexpr *me;

	va_start(va, next_me);
	me = musicexpr_tq(ME_TYPE_SEQUENCE, level, next_me, va);
	va_end(va);

	return me;
}

static struct musicexpr *
musicexpr_simultence(int level, struct musicexpr *next_me, ...)
{
	va_list va;
	struct musicexpr *me;

	va_start(va, next_me);
	me = musicexpr_tq(ME_TYPE_SIMULTENCE, level, next_me, va);
	va_end(va);

	return me;
}

static int
add_musicexpr_to_flat_simultence(struct musicexpr *flatme,
    struct musicexpr *me, float *next_offset, int level)
{
	struct musicexpr *noteoffsetexpr, *p;
	struct musicexpr *marker, *scaled_me, *subexpr;
	float new_next_offset, old_offset;
	size_t i;
	int noteoffset, ret;
	char *me_id1, *me_id2;

	assert(me->me_type != ME_TYPE_RELNOTE);

	new_next_offset = old_offset = *next_offset;

	if ((me_id1 = _mdl_musicexpr_id_string(me)) != NULL) {
		if ((me_id2 = _mdl_musicexpr_id_string(flatme)) != NULL) {
			_mdl_log(MDLLOG_EXPRCONV, level,
			    "inspecting %s for %s\n", me_id1, me_id2);
			free(me_id2);
		}
		free(me_id1);
	}

	level += 1;

	switch (me->me_type) {
	case ME_TYPE_ABSDRUM:
	case ME_TYPE_ABSNOTE:
	case ME_TYPE_MARKER:
	case ME_TYPE_TEMPOCHANGE:
	case ME_TYPE_VOLUMECHANGE:
		ret = add_as_offsetexpr_to_flat_simultence(flatme, me,
		    next_offset, level);
		if (ret != 0)
			return ret;
		break;
	case ME_TYPE_JOINEXPR:
		ret = add_musicexpr_to_flat_simultence(flatme,
		    me->u.joinexpr.a, next_offset, level);
		if (ret != 0)
			return ret;
		marker = _mdl_musicexpr_new(ME_TYPE_MARKER, me->id.textloc,
		    level+1);
		if (ret != 0)
			return ret;
		marker->u.marker.marker_type = ME_MARKER_JOINEXPR;
		ret = add_musicexpr_to_flat_simultence(flatme,
		    me->u.joinexpr.b, next_offset, level);
		if (ret != 0) {
			_mdl_musicexpr_free(marker, level);
			return ret;
		}
		break;
	case ME_TYPE_CHORD:
		noteoffsetexpr = _mdl_chord_to_noteoffsetexpr(me->u.chord,
		    level);
		if (noteoffsetexpr == NULL)
			return 1;
		ret = add_musicexpr_to_flat_simultence(flatme, noteoffsetexpr,
		    next_offset, level);
		_mdl_musicexpr_free(noteoffsetexpr, level);
		if (ret != 0)
			return ret;
		break;
	case ME_TYPE_EMPTY:
		/* Nothing to do. */
		break;
	case ME_TYPE_FLATSIMULTENCE:
		ret = add_musicexpr_to_flat_simultence(flatme,
		    me->u.flatsimultence.me, next_offset, level);
		if (ret != 0)
			return ret;
		*next_offset = MAX(*next_offset,
		    (old_offset + me->u.flatsimultence.length));
		break;
	case ME_TYPE_FUNCTION:
	case ME_TYPE_RELDRUM:
	case ME_TYPE_RELNOTE:
	case ME_TYPE_RELSIMULTENCE:
		/* These should have been handled in previous phases. */
		assert(0);
		break;
	case ME_TYPE_NOTEOFFSETEXPR:
		for (i = 0; i < me->u.noteoffsetexpr.count; i++) {
			subexpr = _mdl_musicexpr_clone(me->u.noteoffsetexpr.me,
			    level);
			if (subexpr == NULL)
				return 1;
			noteoffset = me->u.noteoffsetexpr.offsets[i];
			_mdl_musicexpr_apply_noteoffset(subexpr, noteoffset,
			    level);
			old_offset = *next_offset;
			ret = add_musicexpr_to_flat_simultence(flatme,
			    subexpr, next_offset, level);
			if (ret != 0)
				return ret;
			new_next_offset = MAX(*next_offset, new_next_offset);
			*next_offset = old_offset;
		}
		*next_offset = new_next_offset;
		break;
	case ME_TYPE_OFFSETEXPR:
		*next_offset += me->u.offsetexpr.offset;
		ret = add_musicexpr_to_flat_simultence(flatme,
		    me->u.offsetexpr.me, next_offset, level);
		if (ret != 0)
			return ret;
		break;
	case ME_TYPE_ONTRACK:
		ret = add_musicexpr_to_flat_simultence(flatme,
		    me->u.ontrack.me, next_offset, level);
		if (ret != 0)
			return ret;
		break;
	case ME_TYPE_REST:
		*next_offset += me->u.rest.length;
		break;
	case ME_TYPE_SCALEDEXPR:
		scaled_me =
		    _mdl_musicexpr_scaledexpr_unscale(&me->u.scaledexpr,
		    level);
		if (scaled_me == NULL)
			return 1;
		ret = add_musicexpr_to_flat_simultence(flatme, scaled_me,
		    next_offset, level);
		_mdl_musicexpr_free(scaled_me, level);
		if (ret != 0)
			return ret;
		break;
	case ME_TYPE_SEQUENCE:
		TAILQ_FOREACH(p, &me->u.melist, tq) {
			ret = add_musicexpr_to_flat_simultence(flatme, p,
			    next_offset, level);
			if (ret != 0)
				return ret;
		}
		break;
	case ME_TYPE_SIMULTENCE:
		TAILQ_FOREACH(p, &me->u.melist, tq) {
			old_offset = *next_offset;
			ret = add_musicexpr_to_flat_simultence(flatme, p,
			    next_offset, level);
			if (ret != 0)
				return ret;
			new_next_offset = MAX(*next_offset, new_next_offset);
			*next_offset = old_offset;
		}
		*next_offset = new_next_offset;
		break;
	default:
		assert(0);
	}

	flatme->u.flatsimultence.length = MAX(flatme->u.flatsimultence.length,
	    *next_offset);

	_mdl_log(MDLLOG_EXPRCONV, level, "offset changed from %f to %f\n",
	    old_offset, *next_offset);

	return 0;
}

static int
add_as_offsetexpr_to_flat_simultence(struct musicexpr *flatme,
    struct musicexpr *me, float *next_offset, int level)
{
	struct musicexpr *cloned, *offsetexpr;
	char *me_id1, *me_id2;

	if ((cloned = _mdl_musicexpr_clone(me, level)) == NULL)
		return 1;
	offsetexpr = _mdl_musicexpr_new(ME_TYPE_OFFSETEXPR,
	    _mdl_textloc_zero(), level);
	if (offsetexpr == NULL) {
		_mdl_musicexpr_free(cloned, level+1);
		return 1;
	}
	offsetexpr->u.offsetexpr.me = cloned;
	offsetexpr->u.offsetexpr.offset = *next_offset;

	if ((me_id1 = _mdl_musicexpr_id_string(offsetexpr)) != NULL) {
		me_id2 = _mdl_musicexpr_id_string(flatme);
		if (me_id2 != NULL) {
			_mdl_log(MDLLOG_EXPRCONV, level,
			    "adding %s to %s\n", me_id1, me_id2);
			free(me_id2);
		}
		free(me_id1);
	}

	_mdl_musicexpr_log(offsetexpr, MDLLOG_EXPRCONV, level, NULL);
	TAILQ_INSERT_TAIL(&flatme->u.flatsimultence.me->u.melist,
	    offsetexpr, tq);

	if (me->me_type == ME_TYPE_ABSNOTE) {
		*next_offset += cloned->u.absnote.length;
	} else if (me->me_type == ME_TYPE_ABSDRUM) {
		*next_offset += cloned->u.absdrum.length;
	} else if (me->me_type == ME_TYPE_MARKER ||
	    me->me_type == ME_TYPE_TEMPOCHANGE ||
	    me->me_type == ME_TYPE_VOLUMECHANGE) {
		/* No change to *next_offset. */
	} else {
		assert(0);
	}

	return 0;
}

struct musicexpr *
_mdl_musicexpr_to_flat_simultence(struct musicexpr *me, int level)
{
	struct musicexpr *flatme, *simultence;
	float next_offset;
	int ret;

	if (me->me_type == ME_TYPE_FLATSIMULTENCE)
		return me;

	flatme = _mdl_musicexpr_new(ME_TYPE_FLATSIMULTENCE,
	    _mdl_textloc_zero(), level);
	if (flatme == NULL)
		return NULL;

	flatme->me_type = ME_TYPE_FLATSIMULTENCE;
	flatme->u.flatsimultence.length = 0.0;

	level += 1;

	if ((simultence = musicexpr_simultence(level, NULL)) == NULL) {
		free(flatme);
		return NULL;
	}

	flatme->u.flatsimultence.me = simultence;

	next_offset = 0.0;
	ret = add_musicexpr_to_flat_simultence(flatme, me, &next_offset,
	    level);
	if (ret != 0) {
		warnx("failed to add a musicexpr to flat simultence");
		_mdl_musicexpr_free(flatme, level+1);
		return NULL;
	}

	return flatme;
}

static void
_mdl_musicexpr_apply_noteoffset(struct musicexpr *me, int offset, int level)
{
	struct musicexpr *p;
	struct musicexpr_iter iter;

	/* These types should have been handled in previous phases. */
	assert(me->me_type != ME_TYPE_FUNCTION);
	assert(me->me_type != ME_TYPE_RELDRUM);
	assert(me->me_type != ME_TYPE_RELNOTE);
	assert(me->me_type != ME_TYPE_RELSIMULTENCE);

	level += 1;

	if (me->me_type == ME_TYPE_ABSNOTE) {
		me->u.absnote.note += offset;
		return;
	}

	/* Traverse the subexpressions. */
	iter = _mdl_musicexpr_iter_new(me);
	while ((p = _mdl_musicexpr_iter_next(&iter)) != NULL)
		_mdl_musicexpr_apply_noteoffset(p, offset, level);
}

struct musicexpr *
_mdl_musicexpr_scaledexpr_unscale(struct scaledexpr *se, int level)
{
	return musicexpr_scale_in_time(se->me, se->length, level);
}

static struct musicexpr *
musicexpr_scale_in_time(struct musicexpr *me, float target_length, int level)
{
	struct musicexpr *new_me;
	float me_length;
	char *me_id;

	assert(target_length > 0);

	if ((me_id = _mdl_musicexpr_id_string(me)) != NULL) {
		_mdl_log(MDLLOG_EXPRCONV, level,
		    "scaling musicexpr %s to target length %.3f\n", me_id,
		    target_length);
		free(me_id);
	}

	level += 1;

	me_length = musicexpr_calc_length(me);
	assert(me_length > 0);

	if ((new_me = _mdl_musicexpr_clone(me, level)) == NULL)
		return NULL;

	_mdl_musicexpr_stretch_length(new_me, (target_length / me_length));

	return new_me;
}

static void
_mdl_musicexpr_stretch_length(struct musicexpr *me, float factor)
{
	struct musicexpr *p;
	struct musicexpr_iter iter;

	/* These types should have been handled in previous phases. */
	assert(me->me_type != ME_TYPE_FUNCTION);
	assert(me->me_type != ME_TYPE_RELDRUM);
	assert(me->me_type != ME_TYPE_RELNOTE);
	assert(me->me_type != ME_TYPE_RELSIMULTENCE);

	switch (me->me_type) {
	case ME_TYPE_ABSNOTE:
		me->u.absnote.length *= factor;
		break;
	case ME_TYPE_ABSDRUM:
		me->u.absdrum.length *= factor;
		break;
	case ME_TYPE_FLATSIMULTENCE:
		me->u.flatsimultence.length *= factor;
		break;
	case ME_TYPE_OFFSETEXPR:
		me->u.offsetexpr.offset *= factor;
		break;
	case ME_TYPE_REST:
		me->u.rest.length *= factor;
		break;
	case ME_TYPE_SCALEDEXPR:
		me->u.scaledexpr.length *= factor;
		break;
	default:
		;
	}

	/*
	 * Do not iterate subexpressions with scaled expression,
	 * scaling is already done by adjusting length.
	 */
	if (me->me_type == ME_TYPE_SCALEDEXPR)
		return;

	/* Traverse the subexpressions. */
	iter = _mdl_musicexpr_iter_new(me);
	while ((p = _mdl_musicexpr_iter_next(&iter)) != NULL)
		_mdl_musicexpr_stretch_length(p, factor);
}

static float
musicexpr_calc_length(struct musicexpr *me)
{
	struct musicexpr *p;
	struct musicexpr_iter iter;
	float length, tmp_length;

	/* These should have been handled in previous phases. */
	assert(me->me_type != ME_TYPE_FUNCTION);
	assert(me->me_type != ME_TYPE_RELDRUM);
	assert(me->me_type != ME_TYPE_RELNOTE);
	assert(me->me_type != ME_TYPE_RELSIMULTENCE);

	switch (me->me_type) {
        case ME_TYPE_ABSDRUM:
		return me->u.absdrum.length;
        case ME_TYPE_ABSNOTE:
		return me->u.absnote.length;
        case ME_TYPE_FLATSIMULTENCE:
		return me->u.flatsimultence.length;
        case ME_TYPE_REST:
		return me->u.rest.length;
        case ME_TYPE_SCALEDEXPR:
		return me->u.scaledexpr.length;
	default:
		;
	}

	length = 0.0;

	if (me->me_type == ME_TYPE_OFFSETEXPR)
		length += me->u.offsetexpr.offset;

	/* Traverse the subexpressions. */
	iter = _mdl_musicexpr_iter_new(me);
	while ((p = _mdl_musicexpr_iter_next(&iter)) != NULL) {
		tmp_length = musicexpr_calc_length(p);
		switch (me->me_type) {
		case ME_TYPE_SIMULTENCE:
			length = MAX(length, tmp_length);
			break;
		default:
			length += tmp_length;
		}
	}

	assert(length >= 0.0);

	return length;
}

void
_mdl_musicexpr_log(const struct musicexpr *me, enum logtype logtype, int level,
    char *prefix)
{
	size_t i;
	int ret;
	char *me_id, *old_tmpstring, *tmpstring;

	if ((me_id = _mdl_musicexpr_id_string(me)) == NULL)
		return;

	if (prefix == NULL)
		prefix = "";

	switch (me->me_type) {
	case ME_TYPE_ABSDRUM:
		_mdl_log(logtype, level,
		    "%s%s drumsym=%d note=%d length=%.3f joining=%d"
		    " instrument=\"%s\" track=\"%s\"\n", prefix, me_id,
		    me->u.absdrum.drumsym, me->u.absdrum.note,
		    me->u.absdrum.length, me->joining,
		    me->u.absdrum.instrument->name, me->u.absdrum.track->name);
		break;
	case ME_TYPE_ABSNOTE:
		_mdl_log(logtype, level,
		    "%s%s notesym=%d note=%d length=%.3f joining=%d"
		    " instrument=\"%s\" track=\"%s\"\n", prefix, me_id,
		    me->u.absnote.notesym, me->u.absnote.note,
		    me->u.absnote.length, me->joining,
		    me->u.absnote.instrument->name, me->u.absnote.track->name);
		break;
	case ME_TYPE_CHORD:
		_mdl_log(logtype, level, "%s%s joining=%d\n", prefix, me_id,
		    me->joining);
		_mdl_musicexpr_log_chordtype(me->u.chord.chordtype, logtype,
		    level+1, prefix);
		_mdl_musicexpr_log(me->u.chord.me, logtype, level+1, prefix);
		break;
	case ME_TYPE_EMPTY:
		_mdl_log(logtype, level, "%s%s\n", prefix, me_id);
		break;
	case ME_TYPE_FLATSIMULTENCE:
		_mdl_log(logtype, level, "%s%s length=%f joining=%d\n", prefix,
		    me_id, me->u.flatsimultence.length, me->joining);
		_mdl_musicexpr_log(me->u.flatsimultence.me, logtype, level+1,
		    prefix);
		break;
	case ME_TYPE_FUNCTION:
		/* XXX Printing function arguments might be good? */
		_mdl_log(logtype, level, "%s%s name=%s\n", prefix, me_id,
		    me->u.function.name);
		break;
	case ME_TYPE_JOINEXPR:
		_mdl_log(logtype, level, "%s%s joining=%d\n", prefix, me_id,
		    me->joining);
		_mdl_musicexpr_log(me->u.joinexpr.a, logtype, level+1, prefix);
		_mdl_musicexpr_log(me->u.joinexpr.b, logtype, level+1, prefix);
		break;
	case ME_TYPE_MARKER:
		assert(me->u.marker.marker_type == ME_MARKER_JOINEXPR);
		_mdl_log(logtype, level, "%s%s marker/joinexpr\n", prefix,
		    me_id);
		break;
	case ME_TYPE_NOTEOFFSETEXPR:
		_mdl_log(logtype, level, "%s%s joining=%d\n", prefix, me_id,
		    me->joining);
		_mdl_musicexpr_log(me->u.noteoffsetexpr.me, logtype, level+1,
		    prefix);
		if ((old_tmpstring = strdup("noteoffsets:")) == NULL) {
			free(me_id);
			return;
		}
		for (i = 0; i < me->u.noteoffsetexpr.count; i++) {
			ret = asprintf(&tmpstring, "%s %d", old_tmpstring,
			    me->u.noteoffsetexpr.offsets[i]);
			free(old_tmpstring);
			if (ret == -1) {
				old_tmpstring = NULL;
				break;
			}
			old_tmpstring = tmpstring;
		}
		if (old_tmpstring != NULL) {
			_mdl_log(logtype, level+1, "%s%s\n", prefix,
			    old_tmpstring);
			free(old_tmpstring);
		}
		break;
	case ME_TYPE_OFFSETEXPR:
		_mdl_log(logtype, level, "%s%s offset=%.3f\n", prefix, me_id,
		    me->u.offsetexpr.offset);
		_mdl_musicexpr_log(me->u.offsetexpr.me, logtype, level+1,
		    prefix);
		break;
	case ME_TYPE_ONTRACK:
		_mdl_log(logtype, level, "%s%s track=%s\n", prefix, me_id,
		    me->u.ontrack.track->name);
		_mdl_musicexpr_log(me->u.ontrack.me, logtype, level+1, prefix);
		break;
	case ME_TYPE_RELDRUM:
		_mdl_log(logtype, level,
		    "%s%s drumsym=%d length=%.3f joining=%d\n",
		    prefix, me_id, me->u.reldrum.drumsym,
		    me->u.reldrum.length, me->joining);
		break;
	case ME_TYPE_RELNOTE:
		_mdl_log(logtype, level,
		    "%s%s notesym=%d notemods=%d octavemods=%d length=%.3f"
		    " joining=%d\n",
		    prefix, me_id, me->u.relnote.notesym,
		    me->u.relnote.notemods, me->u.relnote.octavemods,
		    me->u.relnote.length, me->joining);
		break;
	case ME_TYPE_RELSIMULTENCE:
		assert(me->u.scaledexpr.me->me_type == ME_TYPE_SIMULTENCE);
		_mdl_log(logtype, level, "%s%s length=%.3f joining=%d\n",
		    prefix, me_id, me->u.scaledexpr.length, me->joining);
		_mdl_musicexpr_log(me->u.scaledexpr.me, logtype, level+1,
		    prefix);
		break;
	case ME_TYPE_REST:
		_mdl_log(logtype, level, "%s%s length=%.3f joining=%d\n",
		    prefix, me_id, me->u.rest.length, me->joining);
		break;
	case ME_TYPE_SCALEDEXPR:
		_mdl_log(logtype, level, "%s%s length=%.3f joining=%d\n",
		    prefix, me_id, me->u.scaledexpr.length, me->joining);
		_mdl_musicexpr_log(me->u.scaledexpr.me, logtype, level+1,
		    prefix);
		break;
	case ME_TYPE_SEQUENCE:
	case ME_TYPE_SIMULTENCE:
		_mdl_log(logtype, level, "%s%s joining=%d\n", prefix, me_id,
		    me->joining);
		_mdl_musicexpr_log_melist(me->u.melist, logtype, level+1,
		    prefix);
		break;
	case ME_TYPE_TEMPOCHANGE:
		_mdl_log(logtype, level, "%s%s bpm=%.3f\n", prefix, me_id,
		    me->u.tempochange.bpm);
		break;
	case ME_TYPE_VOLUMECHANGE:
		tmpstring = (me->u.volumechange.track != NULL)
			      ? me->u.volumechange.track->name
			      : "(notrack)";
		_mdl_log(logtype, level, "%s%s track=%s volume=%d\n", prefix,
		    me_id, tmpstring, me->u.volumechange.volume);
		break;
	default:
		assert(0);
	}

	free(me_id);
}

static void
_mdl_musicexpr_log_chordtype(enum chordtype chordtype, enum logtype logtype,
    int level, char *prefix)
{
	const char *chordnames[] = {
		"none",		/* CHORDTYPE_NONE     */
		"5",		/* CHORDTYPE_MAJ      */
		"m",		/* CHORDTYPE_MIN      */
		"aug",		/* CHORDTYPE_AUG      */
		"dim",		/* CHORDTYPE_DIM      */
		"7",		/* CHORDTYPE_7        */
		"maj7",		/* CHORDTYPE_MAJ7     */
		"m7",		/* CHORDTYPE_MIN7     */
		"dim7",		/* CHORDTYPE_DIM7     */
		"aug7",		/* CHORDTYPE_AUG7     */
		"m7.5-",	/* CHORDTYPE_DIM5MIN7 */
		"m7+",		/* CHORDTYPE_MIN5MAJ7 */
		"6",		/* CHORDTYPE_MAJ6     */
		"m6",		/* CHORDTYPE_MIN6     */
		"9",		/* CHORDTYPE_9        */
		"maj9",		/* CHORDTYPE_MAJ9     */
		"m9",		/* CHORDTYPE_MIN9     */
		"11",		/* CHORDTYPE_11       */
		"maj11",	/* CHORDTYPE_MAJ11    */
		"m11",		/* CHORDTYPE_MIN11    */
		"13",		/* CHORDTYPE_13       */
		"13.11",	/* CHORDTYPE_13_11    */
		"maj13.11",	/* CHORDTYPE_MAJ13_11 */
		"m13.11",	/* CHORDTYPE_MIN13_11 */
		"sus2",		/* CHORDTYPE_SUS2     */
		"sus4",		/* CHORDTYPE_SUS4     */
		"1.5",		/* CHORDTYPE_5        */
		"1.5.8",	/* CHORDTYPE_5_8      */
	};

	assert(chordtype < CHORDTYPE_MAX);

	_mdl_log(logtype, level, "%schordtype %s\n", prefix,
	    chordnames[chordtype]);
}

static void
_mdl_musicexpr_log_melist(struct melist melist, enum logtype logtype,
    int level, char *prefix)
{
	struct musicexpr *p;

	TAILQ_FOREACH(p, &melist, tq)
		_mdl_musicexpr_log(p, logtype, level, prefix);
}

void
_mdl_musicexpr_free(struct musicexpr *me, int level)
{
	char *me_id;

	if ((me_id = _mdl_musicexpr_id_string(me)) != NULL) {
		_mdl_log(MDLLOG_MM, level, "freeing musicexpr %s\n", me_id);
		free(me_id);
	}

	level += 1;

	_mdl_musicexpr_free_subexprs(me, level);

	free(me);
}

void
_mdl_musicexpr_free_subexprs(struct musicexpr *me, int level)
{
	struct musicexpr *p;
	struct musicexpr_iter iter;

	if (me->me_type == ME_TYPE_SEQUENCE ||
	    me->me_type == ME_TYPE_SIMULTENCE) {
		_mdl_musicexpr_free_melist(me->u.melist, level);
		return;
	}

	/* Traverse the subexpressions. */
	iter = _mdl_musicexpr_iter_new(me);
	while ((p = _mdl_musicexpr_iter_next(&iter)) != NULL) {
		switch (p->me_type) {
		case ME_TYPE_NOTEOFFSETEXPR:
			_mdl_musicexpr_free(p, level);
			free(p->u.noteoffsetexpr.offsets);
			break;
		default:
			_mdl_musicexpr_free(p, level);
		}
	}
}

void
_mdl_musicexpr_free_melist(struct melist melist, int level)
{
	struct musicexpr *p, *q;

	TAILQ_FOREACH_SAFE(p, &melist, tq, q) {
		TAILQ_REMOVE(&melist, p, tq);
		_mdl_musicexpr_free(p, level);
	}
}

struct musicexpr *
_mdl_chord_to_noteoffsetexpr(struct chord chord, int level)
{
	static const struct {
		size_t count;
		int offsets[7];
	} chord_noteoffsets[] = {
		{ 0, {                         } }, /* CHORDTYPE_NONE     */
		{ 3, { 0, 4, 7                 } }, /* CHORDTYPE_MAJ      */
		{ 3, { 0, 3, 7                 } }, /* CHORDTYPE_MIN      */
		{ 3, { 0, 4, 8                 } }, /* CHORDTYPE_AUG      */
		{ 3, { 0, 3, 6                 } }, /* CHORDTYPE_DIM      */
		{ 4, { 0, 4, 7, 10             } }, /* CHORDTYPE_7        */
		{ 4, { 0, 4, 7, 11             } }, /* CHORDTYPE_MAJ7     */
		{ 4, { 0, 3, 7, 10             } }, /* CHORDTYPE_MIN7     */
		{ 4, { 0, 3, 6,  9             } }, /* CHORDTYPE_DIM7     */
		{ 4, { 0, 4, 8, 10             } }, /* CHORDTYPE_AUG7     */
		{ 4, { 0, 3, 6, 10             } }, /* CHORDTYPE_DIM5MIN7 */
		{ 4, { 0, 3, 7, 11             } }, /* CHORDTYPE_MIN5MAJ7 */
		{ 4, { 0, 4, 7,  9             } }, /* CHORDTYPE_MAJ6     */
		{ 4, { 0, 3, 7,  9             } }, /* CHORDTYPE_MIN6     */
		{ 5, { 0, 4, 7, 10, 14         } }, /* CHORDTYPE_9        */
		{ 5, { 0, 4, 7, 11, 14         } }, /* CHORDTYPE_MAJ9     */
		{ 5, { 0, 3, 7, 10, 14         } }, /* CHORDTYPE_MIN9     */
		{ 6, { 0, 4, 7, 10, 14, 17     } }, /* CHORDTYPE_11       */
		{ 6, { 0, 4, 7, 11, 14, 17     } }, /* CHORDTYPE_MAJ11    */
		{ 6, { 0, 3, 7, 10, 14, 17     } }, /* CHORDTYPE_MIN11    */
		{ 6, { 0, 4, 7, 10, 14,     21 } }, /* CHORDTYPE_13       */
		{ 7, { 0, 4, 7, 10, 14, 17, 21 } }, /* CHORDTYPE_13_11    */
		{ 7, { 0, 4, 7, 11, 14, 17, 21 } }, /* CHORDTYPE_MAJ13_11 */
		{ 7, { 0, 3, 7, 10, 14, 17, 21 } }, /* CHORDTYPE_MIN13_11 */
		{ 3, { 0, 2, 7                 } }, /* CHORDTYPE_SUS2     */
		{ 3, { 0, 5, 7                 } }, /* CHORDTYPE_SUS4     */
		{ 2, { 0,    7                 } }, /* CHORDTYPE_5        */
		{ 3, { 0, 7, 12                } }, /* CHORDTYPE_5_8      */
	};

	struct musicexpr *me;
	enum chordtype chordtype;

	me = _mdl_musicexpr_new(ME_TYPE_NOTEOFFSETEXPR, _mdl_textloc_zero(),
	    level);
	if (me == NULL)
		return NULL;

	chordtype = chord.chordtype;

	assert(chord.me->me_type == ME_TYPE_ABSNOTE);
	assert(chordtype < CHORDTYPE_MAX);

	me->u.noteoffsetexpr.me    = _mdl_musicexpr_clone(chord.me, level+1);
	me->u.noteoffsetexpr.count = chord_noteoffsets[chordtype].count;

	me->u.noteoffsetexpr.offsets =
	    malloc(sizeof(chord_noteoffsets[chordtype].offsets));
	if (me->u.noteoffsetexpr.offsets == NULL) {
		warn("malloc in _mdl_chord_to_noteoffsetexpr");
		free(me);
		return NULL;
	}

	memcpy(me->u.noteoffsetexpr.offsets,
	    chord_noteoffsets[chordtype].offsets,
	    chord_noteoffsets[chordtype].count * sizeof(int));

	return me;
}

void
_mdl_free_melist(struct musicexpr *me)
{
	struct musicexpr *p, *q;

	assert(me->me_type == ME_TYPE_SEQUENCE ||
	    me->me_type == ME_TYPE_SIMULTENCE);

	TAILQ_FOREACH_SAFE(p, &me->u.melist, tq, q)
		TAILQ_REMOVE(&me->u.melist, p, tq);
}

char *
_mdl_musicexpr_id_string(const struct musicexpr *me)
{
	static const char *strings[] = {
		"absdrum",		/* ME_TYPE_ABSDRUM */
		"absnote",		/* ME_TYPE_ABSNOTE */
		"chord",		/* ME_TYPE_CHORD */
		"empty",		/* ME_TYPE_EMPTY */
		"flatsimultence",	/* ME_TYPE_FLATSIMULTENCE */
		"function",		/* ME_TYPE_FUNCTION */
		"joinexpr",		/* ME_TYPE_JOINEXPR */
		"marker",		/* ME_TYPE_MARKER */
		"noteoffsetexpr",	/* ME_TYPE_NOTEOFFSETEXPR */
		"offsetexpr",		/* ME_TYPE_OFFSETEXPR */
		"ontrack",		/* ME_TYPE_ONTRACK */
		"reldrum",		/* ME_TYPE_RELDRUM */
		"relnote",		/* ME_TYPE_RELNOTE */
		"relsimultence",	/* ME_TYPE_RELSIMULTENCE */
		"rest",			/* ME_TYPE_REST */
		"scaledexpr",		/* ME_TYPE_SCALEDEXPR */
		"sequence",		/* ME_TYPE_SEQUENCE */
		"simultence",		/* ME_TYPE_SIMULTENCE */
		"tempochange",		/* ME_TYPE_TEMPOCHANGE */
		"volumechange",		/* ME_TYPE_VOLUMECHANGE */
	};
	char *id_string;
	int ret;

	assert(me != NULL);
	assert(me->me_type < ME_TYPE_COUNT);

	ret = asprintf(&id_string, "%s:%d:%d,%d:%d,%d",
	    strings[me->me_type], me->id.id, me->id.textloc.first_line,
	    me->id.textloc.first_column, me->id.textloc.last_line,
	    me->id.textloc.last_column);
	if (ret == -1) {
		warnx("error in asprintf in _mdl_musicexpr_id_string()");
		return NULL;
	}

	return id_string;
}

struct musicexpr *
_mdl_musicexpr_new(enum musicexpr_type me_type, struct textloc textloc,
    int level)
{
	struct musicexpr *me;
	char *me_id;

	if (musicexpr_id_counter == INT_MAX) {
		warnx("%s", "musicexpr id counter overflow");
		return NULL;
	}

	if ((me = malloc(sizeof(struct musicexpr))) == NULL) {
		warn("%s", "malloc error in _mdl_musicexpr_new");
		return NULL;
	}

	me->me_type = me_type;
	me->joining = 0;
	me->id.id = musicexpr_id_counter++;
	me->id.textloc = textloc;

	if ((me_id = _mdl_musicexpr_id_string(me)) != NULL) {
		_mdl_log(MDLLOG_MM, level, "created %s\n", me_id);
		free(me_id);
	}

	return me;
}

struct musicexpr_iter
_mdl_musicexpr_iter_new(struct musicexpr *me)
{
	struct musicexpr_iter iter;

	iter.me = me;
	iter.curr = NULL;

	switch (me->me_type) {
	case ME_TYPE_ABSDRUM:
	case ME_TYPE_ABSNOTE:
	case ME_TYPE_EMPTY:
	case ME_TYPE_FUNCTION:
	case ME_TYPE_MARKER:
	case ME_TYPE_RELDRUM:
	case ME_TYPE_RELNOTE:
	case ME_TYPE_REST:
	case ME_TYPE_TEMPOCHANGE:
	case ME_TYPE_VOLUMECHANGE:
		/* No subexpressions to iterate. */
		break;
	case ME_TYPE_CHORD:
		iter.curr = me->u.chord.me;
		break;
	case ME_TYPE_FLATSIMULTENCE:
		iter.curr = me->u.flatsimultence.me;
		break;
	case ME_TYPE_JOINEXPR:
		iter.curr = me->u.joinexpr.a;
		break;
	case ME_TYPE_NOTEOFFSETEXPR:
		iter.curr = me->u.noteoffsetexpr.me;
		break;
	case ME_TYPE_OFFSETEXPR:
		iter.curr = me->u.offsetexpr.me;
		break;
	case ME_TYPE_ONTRACK:
		iter.curr = me->u.ontrack.me;
		break;
	case ME_TYPE_RELSIMULTENCE:
	case ME_TYPE_SCALEDEXPR:
		iter.curr = me->u.scaledexpr.me;
		break;
	case ME_TYPE_SEQUENCE:
	case ME_TYPE_SIMULTENCE:
		iter.curr = TAILQ_FIRST(&me->u.melist);
		break;
	default:
		assert(0);
	}

	return iter;
}

struct musicexpr *
_mdl_musicexpr_iter_next(struct musicexpr_iter *iter)
{
	struct musicexpr *current;

	if (iter->curr == NULL)
		return NULL;

	current = iter->curr;

	switch (iter->me->me_type) {
	case ME_TYPE_JOINEXPR:
		/* Join expressions have two subexpressions. */
		if (iter->curr == iter->me->u.joinexpr.a) {
			iter->curr = iter->me->u.joinexpr.b;
		} else {
			iter->curr = NULL;
		}
		break;
	case ME_TYPE_SEQUENCE:
	case ME_TYPE_SIMULTENCE:
		/* Sequences and simultences have multiple subexpressions. */
		iter->curr = TAILQ_NEXT(iter->curr, tq);
		break;
	default:
		/* Other types have zero or one subexpressions. */
		assert(iter->me->me_type < ME_TYPE_COUNT);
		iter->curr = NULL;
	}

	return current;
}

void
_mdl_musicexpr_replace(struct musicexpr *dst, struct musicexpr *src,
    enum logtype logtype, int level)
{
	char *dst_id, *src_id;

	if ((dst_id = _mdl_musicexpr_id_string(dst)) != NULL) {
		if ((src_id = _mdl_musicexpr_id_string(src)) != NULL) {
			_mdl_log(logtype, level, "replacing %s with %s\n",
			    dst_id, src_id);
			free(src_id);
		}
		free(dst_id);
	}

	dst->id      = src->id;
	dst->joining = src->joining;
	dst->me_type = src->me_type;
	dst->u       = src->u;
}

void
_mdl_musicexpr_tag_expressions_for_joining(struct musicexpr *me, int level)
{
	struct musicexpr *p;
	struct musicexpr_iter iter;

	level += 1;

	if (me->me_type == ME_TYPE_JOINEXPR)
		tag_as_joining(me->u.joinexpr.a, level);

	/* Traverse the subexpressions. */
	iter = _mdl_musicexpr_iter_new(me);
	while ((p = _mdl_musicexpr_iter_next(&iter)) != NULL)
		_mdl_musicexpr_tag_expressions_for_joining(p, level);
}

static void
tag_as_joining(struct musicexpr *me, int level)
{
	struct musicexpr *p;

	/* Should not happen here. */
	assert(me->me_type != ME_TYPE_FLATSIMULTENCE);
	assert(me->me_type != ME_TYPE_FUNCTION);
	assert(me->me_type != ME_TYPE_OFFSETEXPR);

	me->joining = 1;

	level += 1;

	switch (me->me_type) {
	case ME_TYPE_CHORD:
		assert(me->u.chord.me->me_type == ME_TYPE_ABSNOTE);
		tag_as_joining(me->u.chord.me, level);
		break;
	case ME_TYPE_JOINEXPR:
		tag_as_joining(me->u.joinexpr.b, level);
		break;
	case ME_TYPE_NOTEOFFSETEXPR:
		tag_as_joining(me->u.noteoffsetexpr.me, level);
		break;
	case ME_TYPE_ONTRACK:
		tag_as_joining(me->u.ontrack.me, level);
		break;
	case ME_TYPE_RELSIMULTENCE:
	case ME_TYPE_SCALEDEXPR:
		tag_as_joining(me->u.scaledexpr.me, level);
		break;
	case ME_TYPE_SEQUENCE:
		p = TAILQ_LAST(&me->u.melist, melist);
		if (p != NULL)
			tag_as_joining(p, level);
		break;
	case ME_TYPE_SIMULTENCE:
		TAILQ_FOREACH(p, &me->u.melist, tq)
			tag_as_joining(p, level);
		break;
	default:
		;
	}
}
