/* $Id: musicexpr.c,v 1.100 2016/05/11 20:30:01 je Exp $ */

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

#include "joinexpr.h"
#include "musicexpr.h"
#include "util.h"

int musicexpr_id_counter = 0;

static int	_mdl_musicexpr_clone_melist(struct melist *, struct melist,
    int);
static void	_mdl_musicexpr_log_chordtype(enum chordtype, enum logtype, int,
    char *);
static void	_mdl_musicexpr_log_melist(struct melist, enum logtype, int,
    char *);
static struct musicexpr *musicexpr_tq(enum musicexpr_type me_type,
    int, struct musicexpr *, va_list va);
static struct musicexpr *musicexpr_simultence(int, struct musicexpr *, ...);
static int	add_musicexpr_to_flat_simultence(struct musicexpr *,
    struct musicexpr *, float *, int);
void _mdl_musicexpr_apply_noteoffset(struct musicexpr *, int, int);
static struct musicexpr *musicexpr_scale_in_time(struct musicexpr *, float,
    int);
static float	musicexpr_calc_length(struct musicexpr *);
void		_mdl_musicexpr_stretch_length(struct musicexpr *,
    float);

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

	cloned->u = me->u;

	switch (me->me_type) {
	case ME_TYPE_ABSNOTE:
	case ME_TYPE_EMPTY:
	case ME_TYPE_RELNOTE:
	case ME_TYPE_REST:
		break;
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
		assert(0);
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
	struct musicexpr *cloned, *noteoffsetexpr, *offsetexpr, *p;
	struct musicexpr *scaled_me, *subexpr;
	float new_next_offset, old_offset;
	size_t i;
	int noteoffset, ret;
	char *me_id1, *me_id2;

	assert(me->me_type != ME_TYPE_JOINEXPR);
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
	case ME_TYPE_ABSNOTE:
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

		*next_offset += cloned->u.absnote.length;
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
	case ME_TYPE_JOINEXPR:	/* XXX Is this correct? */
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

void
_mdl_musicexpr_apply_noteoffset(struct musicexpr *me, int offset, int level)
{
	struct musicexpr *p;

	/*
	 * XXX There is probably a common pattern here: do some operation
	 * XXX to all subexpressions... but the knowledge "what are the
	 * XXX subexpressions" is not anywhere.
	 */

	assert(me->me_type != ME_TYPE_RELNOTE);

	level += 1;

	switch (me->me_type) {
	case ME_TYPE_ABSNOTE:
		me->u.absnote.note += offset;
		    break;
	case ME_TYPE_CHORD:
		_mdl_musicexpr_apply_noteoffset(me->u.chord.me, offset, level);
		break;
	case ME_TYPE_EMPTY:
		/* Nothing to do. */
		break;
	case ME_TYPE_FLATSIMULTENCE:
		_mdl_musicexpr_apply_noteoffset(me->u.flatsimultence.me,
		    offset, level);
		break;
	case ME_TYPE_JOINEXPR:
		_mdl_musicexpr_apply_noteoffset(me->u.joinexpr.a, offset,
		    level);
		_mdl_musicexpr_apply_noteoffset(me->u.joinexpr.b, offset,
		    level);
		break;
	case ME_TYPE_NOTEOFFSETEXPR:
		_mdl_musicexpr_apply_noteoffset(me->u.noteoffsetexpr.me,
		    offset, level);
		break;
	case ME_TYPE_OFFSETEXPR:
		_mdl_musicexpr_apply_noteoffset(me->u.offsetexpr.me, offset,
		    level);
		break;
	case ME_TYPE_ONTRACK:
		_mdl_musicexpr_apply_noteoffset(me->u.ontrack.me, offset,
		    level);
		break;
	case ME_TYPE_RELNOTE:
	case ME_TYPE_RELSIMULTENCE:
		/* These should have been handled in previous phases. */
		assert(0);
		break;
	case ME_TYPE_REST:
		/* Nothing to do. */
		break;
	case ME_TYPE_SCALEDEXPR:
		_mdl_musicexpr_apply_noteoffset(me->u.scaledexpr.me, offset,
		    level);
		break;
	case ME_TYPE_SEQUENCE:
	case ME_TYPE_SIMULTENCE:
		TAILQ_FOREACH(p, &me->u.melist, tq)
			_mdl_musicexpr_apply_noteoffset(p, offset, level);
		break;
	default:
		assert(0);
	}
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

void
_mdl_musicexpr_stretch_length(struct musicexpr *me, float factor)
{
	struct musicexpr *p;

	assert(me->me_type != ME_TYPE_RELNOTE);

	switch (me->me_type) {
	case ME_TYPE_ABSNOTE:
		me->u.absnote.length *= factor;
		break;
	case ME_TYPE_CHORD:
		_mdl_musicexpr_stretch_length(me->u.chord.me,
		    factor);
		break;
	case ME_TYPE_EMPTY:
		break;
	case ME_TYPE_FLATSIMULTENCE:
		_mdl_musicexpr_stretch_length(me->u.flatsimultence.me, factor);
		me->u.flatsimultence.length *= factor;
		break;
	case ME_TYPE_JOINEXPR:
		_mdl_musicexpr_stretch_length(me->u.joinexpr.a, factor);
		_mdl_musicexpr_stretch_length(me->u.joinexpr.b, factor);
		break;
	case ME_TYPE_NOTEOFFSETEXPR:
		_mdl_musicexpr_stretch_length(me->u.noteoffsetexpr.me, factor);
		break;
	case ME_TYPE_OFFSETEXPR:
		_mdl_musicexpr_stretch_length(me->u.offsetexpr.me, factor);
		me->u.offsetexpr.offset *= factor;
		break;
	case ME_TYPE_ONTRACK:
		_mdl_musicexpr_stretch_length(me->u.ontrack.me, factor);
		break;
	case ME_TYPE_RELNOTE:
	case ME_TYPE_RELSIMULTENCE:
		/* These should have been handled in previous phases. */
		assert(0);
		break;
	case ME_TYPE_REST:
		me->u.rest.length *= factor;
		break;
	case ME_TYPE_SCALEDEXPR:
		me->u.scaledexpr.length *= factor;
		break;
	case ME_TYPE_SEQUENCE:
	case ME_TYPE_SIMULTENCE:
		TAILQ_FOREACH(p, &me->u.melist, tq)
			_mdl_musicexpr_stretch_length(p, factor);
		break;
	default:
		assert(0);
	}
}

static float
musicexpr_calc_length(struct musicexpr *me)
{
	struct musicexpr *p;
	float length;

	assert(me->me_type != ME_TYPE_RELNOTE);

	length = 0.0;

	switch (me->me_type) {
        case ME_TYPE_ABSNOTE:
		length = me->u.absnote.length;
		break;
        case ME_TYPE_CHORD:
		length = musicexpr_calc_length(me->u.chord.me);
		break;
        case ME_TYPE_EMPTY:
		break;
        case ME_TYPE_FLATSIMULTENCE:
		length = me->u.flatsimultence.length;
		break;
        case ME_TYPE_JOINEXPR:
		length = musicexpr_calc_length(me->u.joinexpr.a) +
		    musicexpr_calc_length(me->u.joinexpr.b);
		break;
        case ME_TYPE_NOTEOFFSETEXPR:
		length = musicexpr_calc_length(me->u.noteoffsetexpr.me);
		break;
        case ME_TYPE_OFFSETEXPR:
		length = me->u.offsetexpr.offset +
		    musicexpr_calc_length(me->u.offsetexpr.me);
		break;
        case ME_TYPE_ONTRACK:
		length = musicexpr_calc_length(me->u.ontrack.me);
		break;
	case ME_TYPE_RELNOTE:
	case ME_TYPE_RELSIMULTENCE:
		/* These should have been handled in previous phases. */
		assert(0);
		break;
        case ME_TYPE_REST:
		length = me->u.rest.length;
		break;
        case ME_TYPE_SCALEDEXPR:
		length = me->u.scaledexpr.length;
		break;
        case ME_TYPE_SEQUENCE:
		TAILQ_FOREACH(p, &me->u.melist, tq)
			length += musicexpr_calc_length(p);
		break;
        case ME_TYPE_SIMULTENCE:
		TAILQ_FOREACH(p, &me->u.melist, tq)
			length = MAX(length, musicexpr_calc_length(p));
		break;
	default:
		assert(0);
	}

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
	case ME_TYPE_ABSNOTE:
		_mdl_log(logtype, level,
		    "%s%s notesym=%d note=%d length=%.3f instrument=\"%s\""
		    " track=\"%s\"\n", prefix, me_id,
		    me->u.absnote.notesym, me->u.absnote.note,
		    me->u.absnote.length, me->u.absnote.instrument->name,
		    me->u.absnote.track->name);
		break;
	case ME_TYPE_CHORD:
		_mdl_log(logtype, level, "%s%s\n", prefix, me_id);
		_mdl_musicexpr_log_chordtype(me->u.chord.chordtype, logtype,
		    level+1, prefix);
		_mdl_musicexpr_log(me->u.chord.me, logtype, level+1, prefix);
		break;
	case ME_TYPE_EMPTY:
		_mdl_log(logtype, level, "%s%s\n", prefix, me_id);
		break;
	case ME_TYPE_FLATSIMULTENCE:
		_mdl_log(logtype, level, "%s%s length=%f\n", prefix, me_id,
		    me->u.flatsimultence.length);
		_mdl_musicexpr_log(me->u.flatsimultence.me, logtype, level+1,
		    prefix);
		break;
	case ME_TYPE_JOINEXPR:
		_mdl_log(logtype, level, "%s%s\n", prefix, me_id);
		_mdl_musicexpr_log(me->u.joinexpr.a, logtype, level+1, prefix);
		_mdl_musicexpr_log(me->u.joinexpr.b, logtype, level+1, prefix);
		break;
	case ME_TYPE_NOTEOFFSETEXPR:
		_mdl_log(logtype, level, "%s%s\n", prefix, me_id);
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
	case ME_TYPE_RELNOTE:
		_mdl_log(logtype, level,
		    "%s%s notesym=%d notemods=%d length=%.3f octavemods=%d\n",
		    prefix, me_id, me->u.relnote.notesym,
		    me->u.relnote.notemods, me->u.relnote.length,
		    me->u.relnote.octavemods);
		break;
	case ME_TYPE_RELSIMULTENCE:
		assert(me->u.scaledexpr.me->me_type == ME_TYPE_SIMULTENCE);
		_mdl_log(logtype, level, "%s%s length=%.3f\n", prefix,
		    me_id, me->u.scaledexpr.length);
		_mdl_musicexpr_log(me->u.scaledexpr.me, logtype, level+1,
		    prefix);
		break;
	case ME_TYPE_REST:
		_mdl_log(logtype, level, "%s%s length=%.3f\n", prefix, me_id,
		    me->u.rest.length);
		break;
	case ME_TYPE_SCALEDEXPR:
		_mdl_log(logtype, level, "%s%s length=%.3f\n", prefix, me_id,
		    me->u.scaledexpr.length);
		_mdl_musicexpr_log(me->u.scaledexpr.me, logtype, level+1,
		    prefix);
		break;
	case ME_TYPE_SEQUENCE:
		_mdl_log(logtype, level, "%s%s\n", prefix, me_id);
		_mdl_musicexpr_log_melist(me->u.melist, logtype, level+1,
		    prefix);
		break;
	case ME_TYPE_SIMULTENCE:
		_mdl_log(logtype, level, "%s%s\n", prefix, me_id);
		_mdl_musicexpr_log_melist(me->u.melist, logtype, level+1,
		    prefix);
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

	switch (me->me_type) {
	case ME_TYPE_ABSNOTE:
	case ME_TYPE_EMPTY:
	case ME_TYPE_RELNOTE:
	case ME_TYPE_REST:
		break;
	case ME_TYPE_CHORD:
		_mdl_musicexpr_free(me->u.chord.me, level);
		break;
	case ME_TYPE_FLATSIMULTENCE:
		_mdl_musicexpr_free(me->u.flatsimultence.me, level);
		break;
	case ME_TYPE_JOINEXPR:
		_mdl_musicexpr_free(me->u.joinexpr.a, level);
		_mdl_musicexpr_free(me->u.joinexpr.b, level);
		break;
	case ME_TYPE_NOTEOFFSETEXPR:
		_mdl_musicexpr_free(me->u.noteoffsetexpr.me, level);
		free(me->u.noteoffsetexpr.offsets);
		break;
	case ME_TYPE_OFFSETEXPR:
		_mdl_musicexpr_free(me->u.offsetexpr.me, level);
		break;
	case ME_TYPE_ONTRACK:
		_mdl_musicexpr_free(me->u.ontrack.me, level);
		break;
	case ME_TYPE_RELSIMULTENCE:
	case ME_TYPE_SCALEDEXPR:
		_mdl_musicexpr_free(me->u.scaledexpr.me, level);
		break;
	case ME_TYPE_SEQUENCE:
	case ME_TYPE_SIMULTENCE:
		_mdl_musicexpr_free_melist(me->u.melist, level);
		break;
	default:
		assert(0);
	}

	free(me);
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
		{ 4, { 0, 3, 5, 10             } }, /* CHORDTYPE_DIM5MIN7 */
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
		{ 2, { 0, 7, 12                } }, /* CHORDTYPE_5_8      */
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
		"absnote",		/* ME_TYPE_ABSNOTE */
		"chord",		/* ME_TYPE_CHORD */
		"empty",		/* ME_TYPE_EMPTY */
		"flatsimultence",	/* ME_TYPE_FLATSIMULTENCE */
		"joinexpr",		/* ME_TYPE_JOINEXPR */
		"noteoffsetexpr",	/* ME_TYPE_NOTEOFFSETEXPR */
		"offsetexpr",		/* ME_TYPE_OFFSETEXPR */
		"ontrack",		/* ME_TYPE_ONTRACK */
		"relnote",		/* ME_TYPE_RELNOTE */
		"relsimultence",	/* ME_TYPE_RELSIMULTENCE */
		"rest",			/* ME_TYPE_REST */
		"scaledexpr",		/* ME_TYPE_SCALEDEXPR */
		"sequence",		/* ME_TYPE_SEQUENCE */
		"simultence",		/* ME_TYPE_SIMULTENCE */
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
	me->id.id = musicexpr_id_counter++;
	me->id.textloc = textloc;

	if ((me_id = _mdl_musicexpr_id_string(me)) != NULL) {
		_mdl_log(MDLLOG_MM, level, "created %s\n", me_id);
		free(me_id);
	}

	return me;
}

struct textloc
_mdl_textloc_zero(void)
{
	struct textloc x;

	x.first_line   = 0;
	x.first_column = 0;
	x.last_line    = 0;
	x.last_column  = 0;

	return x;
}

/* XXX this should probably take variable length arguments */
struct textloc
_mdl_join_textlocs(struct textloc a, struct textloc b)
{
	struct textloc x;

	if (a.first_line == 0)
		return b;

	if (b.first_line == 0)
		return a;

	if (a.first_line < b.first_line) {
		x.first_line   = a.first_line;
		x.first_column = a.first_column;
	} else if (a.first_line > b.first_line) {
		x.first_line   = b.first_line;
		x.first_column = b.first_column;
	} else {
		x.first_line = a.first_line;
		x.first_column = MIN(a.first_column, b.first_column);
	}

	if (a.last_line < b.last_line) {
		x.last_line   = b.last_line;
		x.last_column = b.last_column;
	} else if (a.last_line > b.last_line) {
		x.last_line   = a.last_line;
		x.last_column = a.last_column;
	} else {
		x.last_line = a.last_line;
		x.last_column = MAX(a.last_column, b.last_column);
	}

	return x;
}
