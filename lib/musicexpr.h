/* $Id: musicexpr.h,v 1.4 2015/11/07 20:24:59 je Exp $ */

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

enum notesym_t {
	NOTE_C,
	NOTE_CIS,
	NOTE_DES,
	NOTE_D,
	NOTE_DIS,
	NOTE_ES,
	NOTE_E,
	NOTE_F,
	NOTE_FIS,
	NOTE_GES,
	NOTE_G,
	NOTE_GIS,
	NOTE_AES,
	NOTE_A,
	NOTE_AIS,
	NOTE_BES,
	NOTE_B,
};

struct relnote_t {
	enum notesym_t notesym;
	int dotcount, lengthbase, updown_mod;
};

struct musicexpr_t {
	struct relnote_t relnote;
	struct musicexpr_t *next;
};

void	free_musicexpr(struct musicexpr_t *);

#endif
