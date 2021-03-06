/* $Id: relative.c,v 1.40 2016/09/27 09:22:18 je Exp $ */

/*
 * Copyright (c) 2015, 2016 Juha Erkkil� <je@turnipsi.no-ip.org>
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
#include <sys/types.h>

#include <assert.h>
#include <stdlib.h>

#include "instrument.h"
#include "musicexpr.h"
#include "relative.h"
#include "song.h"
#include "track.h"
#include "util.h"

struct previous_exprs {
	struct instrument	*drum_instrument;
	struct track		*drum_track;
	struct instrument	*toned_instrument;
	struct track		*toned_track;
	float			 length;
	int			 note_no_notemods;
	enum chordtype		 chordtype;
	enum notesym		 notesym;
};

static void	reset_prev_expr_notes(struct previous_exprs *);
static void	relative_to_absolute(struct musicexpr *,
    struct previous_exprs *, int);
static u_int8_t	get_notevalue_for_drumsym(enum drumsym);
static int	compare_notesyms(enum notesym, enum notesym);

void
_mdl_musicexpr_relative_to_absolute(struct song *song, struct musicexpr *me,
    int level)
{
	struct previous_exprs prev_exprs;

	_mdl_log(MDLLOG_RELATIVE, level,
	    "converting relative expressions to absolute\n");

	level += 1;

	prev_exprs.drum_instrument = song->default_drumtrack->instrument;
	assert(prev_exprs.drum_instrument != NULL);
	prev_exprs.drum_track = song->default_drumtrack;

	prev_exprs.toned_instrument = song->default_tonedtrack->instrument;
	assert(prev_exprs.toned_instrument != NULL);
	prev_exprs.toned_track = song->default_tonedtrack;

	reset_prev_expr_notes(&prev_exprs);

	relative_to_absolute(me, &prev_exprs, level);
}

static void
reset_prev_expr_notes(struct previous_exprs *prev_exprs)
{
	prev_exprs->chordtype = CHORDTYPE_MAJ;
	prev_exprs->length = 0.25;
	prev_exprs->note_no_notemods = 60;
	prev_exprs->notesym = NOTE_C;
}

static void
relative_to_absolute(struct musicexpr *me, struct previous_exprs *prev_exprs,
    int level)
{
	struct musicexpr *p;
	struct absdrum absdrum;
	struct absnote absnote;
	struct reldrum reldrum;
	struct relnote relnote;
	struct previous_exprs prev_exprs_copy;
	int notevalues[] = {
		/* For NOTE_C, NOTE_D, ... */
		0, 2, 4, 5, 7, 9, 11,
	};
	int first_note_seen, note, note_no_notemods, c;
	char *me_id;

	if ((me_id = _mdl_musicexpr_id_string(me)) != NULL) {
		_mdl_log(MDLLOG_RELATIVE, level,
		    "rel->abs for expression %s\n", me_id);
		free(me_id);
	}

	level += 1;

	switch (me->me_type) {
	case ME_TYPE_ABSDRUM:
	case ME_TYPE_ABSNOTE:
		/* These music expression types should not occur here. */
		assert(0);
		break;
	case ME_TYPE_CHORD:
		relative_to_absolute(me->u.chord.me, prev_exprs, level);
		if (me->u.chord.chordtype == CHORDTYPE_NONE)
			me->u.chord.chordtype = prev_exprs->chordtype;
		prev_exprs->chordtype = me->u.chord.chordtype;
		break;
	case ME_TYPE_EMPTY:
		break;
	case ME_TYPE_FLATSIMULTENCE:
		if (me->u.flatsimultence.length == 0) {
			me->u.flatsimultence.length = prev_exprs->length;
		} else {
			prev_exprs->length = me->u.flatsimultence.length;
		}
		relative_to_absolute(me->u.flatsimultence.me, prev_exprs,
		    level);
		break;
	case ME_TYPE_FUNCTION:
		/* Functions should not occur here. */
		assert(0);
		break;
	case ME_TYPE_JOINEXPR:
		relative_to_absolute(me->u.joinexpr.a, prev_exprs, level);
		relative_to_absolute(me->u.joinexpr.b, prev_exprs, level);
		break;
	case ME_TYPE_MARKER:
		break;
	case ME_TYPE_NOTEOFFSETEXPR:
		relative_to_absolute(me->u.noteoffsetexpr.me, prev_exprs,
		    level);
		break;
	case ME_TYPE_OFFSETEXPR:
		relative_to_absolute(me->u.offsetexpr.me, prev_exprs, level);
		break;
	case ME_TYPE_ONTRACK:
		/* XXX how to handle drums? really? */

		prev_exprs_copy = *prev_exprs;
		prev_exprs->drum_track = me->u.ontrack.track;
		prev_exprs->toned_track = me->u.ontrack.track;

		prev_exprs->toned_instrument = me->u.ontrack.track->instrument;
		assert(prev_exprs->toned_instrument != NULL);

		prev_exprs->drum_instrument = me->u.ontrack.track->instrument;
		assert(prev_exprs->drum_instrument != NULL);

		relative_to_absolute(me->u.ontrack.me, prev_exprs, level);
		*prev_exprs = prev_exprs_copy;
		break;
	case ME_TYPE_RELDRUM:
		_mdl_musicexpr_log(me, MDLLOG_RELATIVE, level, NULL);

		reldrum = me->u.reldrum;

		assert(reldrum.drumsym < DRUM_MAX);
		assert(reldrum.length >= 0);

		note = get_notevalue_for_drumsym(reldrum.drumsym);

		absdrum.instrument = prev_exprs->drum_instrument;
		absdrum.track = prev_exprs->drum_track;
		absdrum.drumsym = reldrum.drumsym;
		absdrum.length = reldrum.length;
		if (absdrum.length == 0)
			absdrum.length = prev_exprs->length;
		absdrum.note = note;

		me->me_type = ME_TYPE_ABSDRUM;
		me->u.absdrum = absdrum;

		prev_exprs->length = absdrum.length;

		break;
	case ME_TYPE_RELNOTE:
		_mdl_musicexpr_log(me, MDLLOG_RELATIVE, level, NULL);

		relnote = me->u.relnote;

		assert(relnote.notesym < NOTE_MAX);
		assert(relnote.length >= 0);

		note_no_notemods = 12 * (prev_exprs->note_no_notemods / 12) +
		    notevalues[relnote.notesym];

		c = compare_notesyms(prev_exprs->notesym, relnote.notesym);
		if (c > 0 && prev_exprs->note_no_notemods > note_no_notemods) {
			note_no_notemods += 12;
		} else if (c < 0
		    && prev_exprs->note_no_notemods < note_no_notemods) {
			note_no_notemods -= 12;
		}

		note_no_notemods += 12 * relnote.octavemods;

		note = note_no_notemods + relnote.notemods;

		absnote.instrument = prev_exprs->toned_instrument;
		absnote.track = prev_exprs->toned_track;
		absnote.notesym = relnote.notesym;
		absnote.length = relnote.length;
		if (absnote.length == 0)
			absnote.length = prev_exprs->length;
		absnote.note = note;

		me->me_type = ME_TYPE_ABSNOTE;
		me->u.absnote = absnote;

		prev_exprs->length           = absnote.length;
		prev_exprs->note_no_notemods = note_no_notemods;
		prev_exprs->notesym          = absnote.notesym;

		break;
	case ME_TYPE_REST:
		_mdl_musicexpr_log(me, MDLLOG_RELATIVE, level, NULL);

		if (me->u.rest.length == 0) {
			me->u.rest.length = prev_exprs->length;
		} else {
			prev_exprs->length = me->u.rest.length;
		}
		break;
	case ME_TYPE_RELSIMULTENCE:
		assert(me->u.scaledexpr.me->me_type == ME_TYPE_SIMULTENCE);

		/*
		 * In case value for scaled expression length is missing,
		 * get it from previous length.
		 */
		if (me->u.scaledexpr.length == 0)
			me->u.scaledexpr.length = prev_exprs->length;

		/*
		 * Order should not generally matter in simultences, except
		 * in this case we let relativity to affect simultence
		 * notes.  The first note should affect relativity
		 * (not default length), but not subsequent ones.
		 */
		first_note_seen = 0;
		prev_exprs_copy = *prev_exprs;
		TAILQ_FOREACH(p, &me->u.scaledexpr.me->u.melist, tq) {
			relative_to_absolute(p, &prev_exprs_copy, level);
			if (!first_note_seen)
				*prev_exprs = prev_exprs_copy;
			first_note_seen = 1;
		}

		/* We also set default length for subsequent expressions. */
		prev_exprs->length = me->u.scaledexpr.length;

		/*
		 * relsimultence can now be treated like normal
		 * scaled expression.
		 */
		me->me_type = ME_TYPE_SCALEDEXPR;

		break;
	case ME_TYPE_SCALEDEXPR:
		relative_to_absolute(me->u.scaledexpr.me, prev_exprs, level);
		prev_exprs->length = me->u.scaledexpr.length;
		break;
	case ME_TYPE_SEQUENCE:
		/*
		 * Reset previous expression notes for each sequence,
		 * but sequence items do affect subsequent sequence items.
		 */
		prev_exprs_copy = *prev_exprs;
		reset_prev_expr_notes(&prev_exprs_copy);
		TAILQ_FOREACH(p, &me->u.melist, tq)
			relative_to_absolute(p, &prev_exprs_copy, level);
		break;
	case ME_TYPE_SIMULTENCE:
		/*
		 * Reset previous expression notes for each simultence item,
		 * but simultence items do *not* affect subsequent simultence
		 * items.
		 */
		TAILQ_FOREACH(p, &me->u.melist, tq) {
			prev_exprs_copy = *prev_exprs;
			reset_prev_expr_notes(&prev_exprs_copy);
			relative_to_absolute(p, &prev_exprs_copy, level);
		}
		break;
	case ME_TYPE_TEMPOCHANGE:
		break;
	case ME_TYPE_VOLUMECHANGE:
		/* XXX What about adjusting drum volumes? (now we choose
		 * XXX to change volume on the toned instrument track) */
		me->u.volumechange.track = prev_exprs->toned_track;
		break;
	default:
		assert(0);
	}

	if (me->me_type == ME_TYPE_ABSDRUM ||
	    me->me_type == ME_TYPE_ABSNOTE ||
	    me->me_type == ME_TYPE_REST)
		_mdl_musicexpr_log(me, MDLLOG_RELATIVE, level+1, "--> ");
}

static u_int8_t
get_notevalue_for_drumsym(enum drumsym drumsym)
{
	static const u_int8_t drummidinotenumbers[] = {
		35,	/* DRUM_BDA,	acousticbassdrum */
		36,	/* DRUM_BD,	bassdrum         */
		37,	/* DRUM_SSH,	hisidestick      */
		37,	/* DRUM_SS,	sidestick        */
		37,	/* DRUM_SSL,	losidestick      */
		38,	/* DRUM_SNA,	acousticsnare    */
		38,	/* DRUM_SN,	snare            */
		39,	/* DRUM_HC,	handclap         */
		40,	/* DRUM_SNE,	electricsnare    */
		41,	/* DRUM_TOMFL,	lowfloortom      */
		42,	/* DRUM_HHC,	closedhihat      */
		42,	/* DRUM_HH,	hihat            */
		43,	/* DRUM_TOMFH,	highfloortom     */
		44,	/* DRUM_HHP,	pedalhihat       */
		45,	/* DRUM_TOML,	lowtom           */
		46,	/* DRUM_HHO,	openhihat        */
		46,	/* DRUM_HHHO,	halfopenhihat    */
		47,	/* DRUM_TOMML,	lowmidtom        */
		48,	/* DRUM_TOMMH,	himidtom         */
		49,	/* DRUM_CYMCA,	crashcymbala     */
		49,	/* DRUM_CYMC,	crashcymbal      */
		50,	/* DRUM_TOMH,	hightom          */
		51,	/* DRUM_CYMRA,	ridecymbala      */
		51,	/* DRUM_CYMR,	ridecymbal       */
		52,	/* DRUM_CYMCH,	chinesecymbal    */
		53,	/* DRUM_RB,	ridebell         */
		54,	/* DRUM_TAMB,	tambourine       */
		55,	/* DRUM_CYMS,	splashcymbal     */
		56,	/* DRUM_CB,	cowbell          */
		57,	/* DRUM_CYMCB,	crashcymbalb     */
		58,	/* DRUM_VIBS,	vibraslap        */
		59,	/* DRUM_CYMRB,	ridecymbalb      */
		60,	/* DRUM_BOHM,	mutehibongo      */
		60,	/* DRUM_BOH,	hibongo          */
		60,	/* DRUM_BOHO,	openhibongo      */
		61,	/* DRUM_BOLM,	mutelobongo      */
		61,	/* DRUM_BOL,	lobongo          */
		61,	/* DRUM_BOLO,	openlobongo      */
		62,	/* DRUM_CGHM,	mutehiconga      */
		62,	/* DRUM_CGLM,	muteloconga      */
		63,	/* DRUM_CGHO,	openhiconga      */
		63,	/* DRUM_CGH,	hiconga          */
		64,	/* DRUM_CGLO,	openloconga      */
		64,	/* DRUM_CGL,	loconga          */
		65,	/* DRUM_TIMH,	hitimbale        */
		66,	/* DRUM_TIML,	lotimbale        */
		67,	/* DRUM_AGH,	hiagogo          */
		68,	/* DRUM_AGL,	loagogo          */
		69,	/* DRUM_CAB,	cabasa           */
		70,	/* DRUM_MAR,	maracas          */
		71,	/* DRUM_WHS,	shortwhistle     */
		72,	/* DRUM_WHL,	longwhistle      */
		73,	/* DRUM_GUIS,	shortguiro       */
		74,	/* DRUM_GUIL,	longguiro        */
		74,	/* DRUM_GUI,	guiro            */
		75,	/* DRUM_CL,	claves           */
		76,	/* DRUM_WBH,	hiwoodblock      */
		77,	/* DRUM_WBL,	lowoodblock      */
		78,	/* DRUM_CUIM,	mutecuica        */
		79,	/* DRUM_CUIO,	opencuica        */
		80,	/* DRUM_TRIM,	mutetriangle     */
		81,	/* DRUM_TRI,	triangle         */
		81,	/* DRUM_TRIO,	opentriangle     */
	};

	assert(drumsym < DRUM_MAX);

	return drummidinotenumbers[drumsym];
}

/*
 * If equal                                 -->  0.
 * If higher y is closer to x than lower  y -->  1.
 * if lower  y is closer to x than higher y --> -1.
 */
static int
compare_notesyms(enum notesym x, enum notesym y)
{
	int diff;

	assert(x < NOTE_MAX);
	assert(y < NOTE_MAX);

	if (x == y)
		return 0;

	diff = x - y;
	if (diff < 0)
		diff += NOTE_MAX;

	return (diff < 4) ? -1 : 1;
}
