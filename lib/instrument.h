/* $Id: instrument.h,v 1.6 2016/08/23 20:22:58 je Exp $ */

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

#ifndef MDL_INSTRUMENT_H
#define MDL_INSTRUMENT_H

#include "util.h"

enum instrument_type { INSTR_DRUMKIT, INSTR_TONED };

#define DEFAULT_DRUMKIT			"drums"
#define DEFAULT_TONED_INSTRUMENT	"acoustic grand"

#define LONGEST_DRUMKIT_SIZE    sizeof("electronic drums")
#define LONGEST_TONED_SIZE      sizeof("acoustic guitar (nylon)")
#define LONGEST_INSTRUMENT_SIZE MAX(LONGEST_DRUMKIT_SIZE, LONGEST_TONED_SIZE)

struct instrument {
	enum instrument_type type;
	const char name[ LONGEST_INSTRUMENT_SIZE ];
	u_int8_t code;
};

__BEGIN_DECLS
struct instrument *_mdl_get_instrument(enum instrument_type, const char *);
__END_DECLS

#endif /* !MDL_INSTRUMENT_H */
