/* $Id: util.h,v 1.13 2016/01/07 21:15:16 je Exp $ */

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

#ifndef MDL_UTIL_H
#define MDL_UTIL_H

#include <unistd.h>

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

struct mdl_stream {
	size_t count, slotcount;
	enum streamtype { OFFSETEXPRSTREAM, MIDIEVENTSTREAM } s_type;
	union {
		struct musicexpr_with_offset_t *mexprs;
		struct midievent	       *midievents;
	};
};

void	mdl_log(int, int, const char *, ...);

struct mdl_stream      *mdl_stream_new(enum streamtype);
int			mdl_stream_increment(struct mdl_stream *);
void			mdl_stream_free(struct mdl_stream *);
void __dead		unimplemented(void);

#endif
