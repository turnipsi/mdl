/* $Id: instrument.h,v 1.2 2016/01/27 21:34:13 je Exp $ */

/*
 * Copyright (c) 2015 Juha Erkkil� <je@turnipsi.no-ip.org>
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

#ifndef MDL_INSTRUMENT_H
#define MDL_INSTRUMENT_H

#include "util.h"

enum instrument_type_t { INSTR_DRUMKIT, INSTR_TONED };

#define LONGEST_DRUMKIT_SIZE    sizeof("electronic drums")
#define LONGEST_TONED_SIZE      sizeof("acoustic guitar (nylon)")
#define LONGEST_INSTRUMENT_SIZE MAX(LONGEST_DRUMKIT_SIZE, LONGEST_TONED_SIZE)

struct instrument_t {
	enum instrument_type_t type;
	const char name[ LONGEST_INSTRUMENT_SIZE ];
	u_int8_t code;
};

struct instrument_t *
get_instrument(enum instrument_type_t, char *);

#endif