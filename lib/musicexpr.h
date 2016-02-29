/* $Id: musicexpr.h,v 1.58 2016/02/29 21:09:29 je Exp $ */

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

#ifndef MDL_MUSICEXPR_H
#define MDL_MUSICEXPR_H

#include <sys/queue.h>

#include "instrument.h"
#include "track.h"
#include "util.h"

#define MINIMUM_MUSICEXPR_LENGTH	0.0001

enum musicexpr_type {
	ME_TYPE_ABSNOTE,
	ME_TYPE_CHORD,
	ME_TYPE_EMPTY,
	ME_TYPE_FLATSIMULTENCE,
	ME_TYPE_JOINEXPR,
	ME_TYPE_NOTEOFFSETEXPR,
	ME_TYPE_OFFSETEXPR,
	ME_TYPE_ONTRACK,
	ME_TYPE_RELNOTE,
	ME_TYPE_RELSIMULTENCE,
	ME_TYPE_REST,
	ME_TYPE_SCALEDEXPR,
	ME_TYPE_SEQUENCE,
	ME_TYPE_SIMULTENCE,
	ME_TYPE_COUNT,
};

enum notesym {
	NOTE_C, NOTE_D, NOTE_E, NOTE_F, NOTE_G, NOTE_A, NOTE_B, NOTE_MAX,
};

enum notemod { NOTEMOD_ES, NOTEMOD_IS, };

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

struct absnote {
	struct instrument *instrument;
	struct track *track;
	enum notesym notesym;
	float length;
	int note;
};

struct relnote {
	enum notesym notesym;
	float length;
	int notemods, octavemods;
};

struct rest {
	float length;
};

struct chord {
	enum chordtype chordtype;
	struct musicexpr *me;
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
	struct musicexpr *a, *b;
};

struct scaledexpr {
	struct musicexpr *me;
	float length;
};

struct ontrack {
	struct musicexpr *me;
	struct track *track;
};

TAILQ_HEAD(melist, musicexpr);

struct flatsimultence {
	struct musicexpr *me;
	float length;
};

struct musicexpr {
	enum musicexpr_type me_type;
	union {
		struct absnote		absnote;
		struct chord		chord;
		struct flatsimultence	flatsimultence;
		struct joinexpr		joinexpr;
		struct melist		melist;
		struct noteoffsetexpr	noteoffsetexpr;
		struct offsetexpr	offsetexpr;
		struct ontrack		ontrack;
		struct relnote		relnote;
		struct rest		rest;
		struct scaledexpr	scaledexpr;
	} u;
	TAILQ_ENTRY(musicexpr) tq;
};

void	musicexpr_free(struct musicexpr *);
void	musicexpr_log(const struct musicexpr *, enum logtype, int, char *);

struct musicexpr       *musicexpr_clone(struct musicexpr *, int);
struct musicexpr       *musicexpr_sequence(struct musicexpr *, ...);
struct musicexpr       *chord_to_noteoffsetexpr(struct chord, int);
struct musicexpr       *musicexpr_to_flat_simultence(struct musicexpr *, int);
struct musicexpr       *musicexpr_scaledexpr_unscale(struct scaledexpr *, int);
const char	       *musicexpr_type_to_string(const struct musicexpr *);
void			musicexpr_free_melist(struct melist);
void			free_melist(struct musicexpr *);
void			musicexpr_copy(struct musicexpr *, struct musicexpr *);
void			musicexpr_apply_noteoffset(struct musicexpr *, int,
    int);

#endif
