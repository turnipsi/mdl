/* $Id: musicexpr.h,v 1.85 2016/08/27 18:23:07 je Exp $ */

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

#ifndef MDL_MUSICEXPR_H
#define MDL_MUSICEXPR_H

#include <sys/queue.h>

#include "functions.h"
#include "instrument.h"
#include "textloc.h"
#include "track.h"
#include "util.h"

#define MINIMUM_MUSICEXPR_LENGTH	0.0001

enum musicexpr_type {
	ME_TYPE_ABSDRUM,
	ME_TYPE_ABSNOTE,
	ME_TYPE_CHORD,
	ME_TYPE_EMPTY,
	ME_TYPE_FLATSIMULTENCE,
	ME_TYPE_FUNCTION,
	ME_TYPE_JOINEXPR,
	ME_TYPE_NOTEOFFSETEXPR,
	ME_TYPE_OFFSETEXPR,
	ME_TYPE_ONTRACK,
	ME_TYPE_RELDRUM,
	ME_TYPE_RELNOTE,
	ME_TYPE_RELSIMULTENCE,
	ME_TYPE_REST,
	ME_TYPE_SCALEDEXPR,
	ME_TYPE_SEQUENCE,
	ME_TYPE_SIMULTENCE,
	ME_TYPE_TEMPOCHANGE,
	ME_TYPE_VOLUMECHANGE,
	ME_TYPE_COUNT,		/* not a type */
};

enum notesym {
	NOTE_C,
	NOTE_D,
	NOTE_E,
	NOTE_F,
	NOTE_G,
	NOTE_A,
	NOTE_B,
	NOTE_MAX,	/* not a note */
};

enum notemod { NOTEMOD_ES, NOTEMOD_IS, };

enum drumsym {
	DRUM_BDA,	/* acousticbassdrum */
	DRUM_BD,	/* bassdrum         */
	DRUM_SSH,	/* hisidestick      */
	DRUM_SS,	/* sidestick        */
	DRUM_SSL,	/* losidestick      */
	DRUM_SNA,	/* acousticsnare    */
	DRUM_SN,	/* snare            */
	DRUM_HC,	/* handclap         */
	DRUM_SNE,	/* electricsnare    */
	DRUM_TOMFL,	/* lowfloortom      */
	DRUM_HHC,	/* closedhihat      */
	DRUM_HH,	/* hihat            */
	DRUM_TOMFH,	/* highfloortom     */
	DRUM_HHP,	/* pedalhihat       */
	DRUM_TOML,	/* lowtom           */
	DRUM_HHO,	/* openhihat        */
	DRUM_HHHO,	/* halfopenhihat    */
	DRUM_TOMML,	/* lowmidtom        */
	DRUM_TOMMH,	/* himidtom         */
	DRUM_CYMCA,	/* crashcymbala     */
	DRUM_CYMC,	/* crashcymbal      */
	DRUM_TOMH,	/* hightom          */
	DRUM_CYMRA,	/* ridecymbala      */
	DRUM_CYMR,	/* ridecymbal       */
	DRUM_CYMCH,	/* chinesecymbal    */
	DRUM_RB,	/* ridebell         */
	DRUM_TAMB,	/* tambourine       */
	DRUM_CYMS,	/* splashcymbal     */
	DRUM_CB,	/* cowbell          */
	DRUM_CYMCB,	/* crashcymbalb     */
	DRUM_VIBS,	/* vibraslap        */
	DRUM_CYMRB,	/* ridecymbalb      */
	DRUM_BOHM,	/* mutehibongo      */
	DRUM_BOH,	/* hibongo          */
	DRUM_BOHO,	/* openhibongo      */
	DRUM_BOLM,	/* mutelobongo      */
	DRUM_BOL,	/* lobongo          */
	DRUM_BOLO,	/* openlobongo      */
	DRUM_CGHM,	/* mutehiconga      */
	DRUM_CGLM,	/* muteloconga      */
	DRUM_CGHO,	/* openhiconga      */
	DRUM_CGH,	/* hiconga          */
	DRUM_CGLO,	/* openloconga      */
	DRUM_CGL,	/* loconga          */
	DRUM_TIMH,	/* hitimbale        */
	DRUM_TIML,	/* lotimbale        */
	DRUM_AGH,	/* hiagogo          */
	DRUM_AGL,	/* loagogo          */
	DRUM_CAB,	/* cabasa           */
	DRUM_MAR,	/* maracas          */
	DRUM_WHS,	/* shortwhistle     */
	DRUM_WHL,	/* longwhistle      */
	DRUM_GUIS,	/* shortguiro       */
	DRUM_GUIL,	/* longguiro        */
	DRUM_GUI,	/* guiro            */
	DRUM_CL,	/* claves           */
	DRUM_WBH,	/* hiwoodblock      */
	DRUM_WBL,	/* lowoodblock      */
	DRUM_CUIM,	/* mutecuica        */
	DRUM_CUIO,	/* opencuica        */
	DRUM_TRIM,	/* mutetriangle     */
	DRUM_TRI,	/* triangle         */
	DRUM_TRIO,	/* opentriangle     */
	DRUM_MAX,	/* NOT A DRUM       */
};

enum chordtype {
	CHORDTYPE_NONE,
	CHORDTYPE_MAJ,
	CHORDTYPE_MIN,
	CHORDTYPE_AUG,
	CHORDTYPE_DIM,
	CHORDTYPE_7,
	CHORDTYPE_MAJ7,
	CHORDTYPE_MIN7,
	CHORDTYPE_DIM7,
	CHORDTYPE_AUG7,
	CHORDTYPE_DIM5MIN7,
	CHORDTYPE_MIN5MAJ7,
	CHORDTYPE_MAJ6,
	CHORDTYPE_MIN6,
	CHORDTYPE_9,
	CHORDTYPE_MAJ9,
	CHORDTYPE_MIN9,
	CHORDTYPE_11,
	CHORDTYPE_MAJ11,
	CHORDTYPE_MIN11,
	CHORDTYPE_13,
	CHORDTYPE_13_11,
	CHORDTYPE_MAJ13_11,
	CHORDTYPE_MIN13_11,
	CHORDTYPE_SUS2,
	CHORDTYPE_SUS4,
	CHORDTYPE_5,
	CHORDTYPE_5_8,
	CHORDTYPE_MAX,	/* not a chord */
};

struct absdrum {
	struct instrument      *instrument;
	struct track	       *track;
	enum drumsym		drumsym;
	float			length;
	int			note;
};

struct absnote {
	struct instrument      *instrument;
	struct track	       *track;
	enum notesym		notesym;
	float			length;
	int			note;
};

struct reldrum {
	enum drumsym	drumsym;
	float		length;
};

struct relnote {
	enum notesym	notesym;
	float		length;
	int		notemods;
	int		octavemods;
};

struct rest {
	float	length;
};

struct chord {
	enum chordtype		chordtype;
	struct musicexpr       *me;
};

struct noteoffsetexpr {
	struct musicexpr       *me;
	int		       *offsets;
	size_t			count;
};

struct offsetexpr {
	float			offset;
	struct musicexpr       *me;
};

struct joinexpr {
	struct musicexpr       *a;
	struct musicexpr       *b;
};

struct scaledexpr {
	struct musicexpr       *me;
	float			length;
};

struct ontrack {
	struct musicexpr       *me;
	struct track           *track;
};

TAILQ_HEAD(melist, musicexpr);

struct flatsimultence {
	struct musicexpr       *me;
	float			length;
};

struct tempochange {
	float	bpm;
};

struct volumechange {
	struct track	       *track;
	u_int8_t		volume;
};

struct musicexpr_id {
	int		id;
	struct textloc	textloc;
};

struct musicexpr {
	struct musicexpr_id	id;
	enum musicexpr_type	me_type;
	union {
		struct absnote		absnote;
		struct absdrum		absdrum;
		struct chord		chord;
		struct flatsimultence	flatsimultence;
		struct function		function;
		struct joinexpr		joinexpr;
		struct melist		melist;
		struct noteoffsetexpr	noteoffsetexpr;
		struct offsetexpr	offsetexpr;
		struct ontrack		ontrack;
		struct reldrum		reldrum;
		struct relnote		relnote;
		struct rest		rest;
		struct scaledexpr	scaledexpr;
		struct tempochange	tempochange;
		struct volumechange	volumechange;
	} u;
	TAILQ_ENTRY(musicexpr) tq;
};

struct musicexpr_iter {
	struct musicexpr	*me;
	struct musicexpr	*curr;
};

__BEGIN_DECLS
struct musicexpr       *_mdl_chord_to_noteoffsetexpr(struct chord, int);
void			_mdl_free_melist(struct musicexpr *);
struct musicexpr       *_mdl_musicexpr_clone(struct musicexpr *, int);
void			_mdl_musicexpr_free(struct musicexpr *, int);
void			_mdl_musicexpr_free_melist(struct melist, int);
void			_mdl_musicexpr_free_subexprs(struct musicexpr *, int);
char		       *_mdl_musicexpr_id_string(const struct musicexpr *);
struct musicexpr_iter	_mdl_musicexpr_iter_new(struct musicexpr *);
struct musicexpr       *_mdl_musicexpr_iter_next(struct musicexpr_iter *);
void			_mdl_musicexpr_log(const struct musicexpr *,
    enum logtype, int, char *);
struct musicexpr       *_mdl_musicexpr_new(enum musicexpr_type,
    struct textloc, int);
void			_mdl_musicexpr_replace(struct musicexpr *,
    struct musicexpr *, enum logtype, int);
struct musicexpr       *_mdl_musicexpr_scaledexpr_unscale(struct scaledexpr *,
    int);
struct musicexpr       *_mdl_musicexpr_sequence(int, struct musicexpr *, ...);
struct musicexpr       *_mdl_musicexpr_to_flat_simultence(struct musicexpr *,
    int);
__END_DECLS

#endif /* !MDL_MUSICEXPR_H */
