/* $Id: musicexpr.h,v 1.35 2015/12/22 20:23:52 je Exp $ */

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

#include "util.h"

enum musicexpr_type {
	ME_TYPE_ABSNOTE,
	ME_TYPE_RELNOTE,
	ME_TYPE_REST,
	ME_TYPE_SEQUENCE,
	ME_TYPE_WITHOFFSET,
	ME_TYPE_JOINEXPR,
	ME_TYPE_CHORD,
	ME_TYPE_NOTEOFFSETEXPR,
	ME_TYPE_SIMULTENCE
};

enum notesym_t {
	NOTE_C, NOTE_D, NOTE_E, NOTE_F, NOTE_G, NOTE_A, NOTE_B, NOTE_MAX,
};

enum notemod_t { NOTEMOD_ES, NOTEMOD_IS, };

enum chordtype_t {
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

struct absnote_t {
	enum notesym_t notesym;
	float length;
	int note;
};

struct relnote_t {
	enum notesym_t notesym;
	float length;
	int notemods, octavemods;
};

struct rest_t {
	float length;
};

struct chord_t {
	enum chordtype_t chordtype;
	struct musicexpr_t *me;
};

struct noteoffsetexpr_t {
	struct musicexpr_t     *me;
	int		       *offsets;
	size_t			count;
};

struct musicexpr_with_offset_t {
	float			offset;
	struct musicexpr_t     *me;
};

TAILQ_HEAD(sequence_t, tqitem_me);
TAILQ_HEAD(simultence_t, tqitem_me);
struct tqitem_me {
	struct musicexpr_t     *me;
	TAILQ_ENTRY(tqitem_me)	tq;
};

struct previous_relative_exprs_t {
	struct absnote_t absnote;
	enum chordtype_t chordtype;
};

struct joinexpr_t {
	struct musicexpr_t *a, *b;
};

struct musicexpr_t {
	enum musicexpr_type me_type;
	union {
		struct absnote_t		absnote;
		struct joinexpr_t		joinexpr;
		struct relnote_t		relnote;
		struct chord_t			chord;
		struct rest_t			rest;
		struct sequence_t		sequence;
		struct musicexpr_with_offset_t	offsetexpr;
		struct noteoffsetexpr_t		noteoffsetexpr;
		struct simultence_t		simultence;
	};
};

void	musicexpr_free(struct musicexpr_t *);
void	musicexpr_free_sequence(struct sequence_t);
void	musicexpr_log(const struct musicexpr_t *, int, int);

struct musicexpr_t	*musicexpr_sequence(struct musicexpr_t *, ...);
struct musicexpr_t	*chord_to_noteoffsetexpr(struct chord_t, int);

struct mdl_stream	*musicexpr_to_midievents(struct musicexpr_t *, int);

#endif
