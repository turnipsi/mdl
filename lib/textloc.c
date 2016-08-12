/* $Id: textloc.c,v 1.1 2016/08/12 18:16:07 je Exp $ */

/*
 * Copyright (c) 2016 Juha Erkkilä <je@turnipsi.no-ip.org>
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

#include "textloc.h"
#include "util.h"

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
