/* $Id: musicexpr.h,v 1.20 2015/11/28 18:03:18 je Exp $ */

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

#include "util.h"

enum musicexpr_type {
	ME_TYPE_ABSNOTE,
	ME_TYPE_RELNOTE,
	ME_TYPE_SEQUENCE,
	ME_TYPE_WITHOFFSET,
	ME_TYPE_JOINEXPR,
};

enum notesym_t {
  NOTE_C, NOTE_D, NOTE_E, NOTE_F, NOTE_G, NOTE_A, NOTE_B, NOTE_MAX
};

enum notemod_t { NOTEMOD_ES, NOTEMOD_IS, };

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

struct musicexpr_with_offset_t {
	float			offset;
	struct musicexpr_t     *me;
};

struct sequence_t {
	struct musicexpr_t     *me;
	struct sequence_t      *next;
};

struct musicexpr_t {
	enum musicexpr_type me_type;
	union {
		struct absnote_t		absnote;
		struct relnote_t		relnote;
		struct sequence_t	       *sequence;
		struct musicexpr_with_offset_t	offset_expr;
	};
};

struct offsetexprstream_t {
	struct musicexpr_with_offset_t *mexprs;
	struct streamparams		params;
};


void	musicexpr_free(struct musicexpr_t *);
void	musicexpr_free_sequence(struct sequence_t *);
void	musicexpr_log(struct musicexpr_t *, int, int);
void	musicexpr_log_sequence(struct sequence_t *, int, int);

struct midieventstream *musicexpr_to_midievents(struct musicexpr_t *, int);

#endif
