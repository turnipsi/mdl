/* $Id: textloc.h,v 1.1 2016/08/12 18:16:07 je Exp $ */

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

#ifndef MDL_TEXTLOC_H
#define MDL_TEXTLOC_H

struct textloc {
	int first_line;
	int last_line;
	int first_column;
	int last_column;
};

__BEGIN_DECLS
struct textloc	_mdl_join_textlocs(struct textloc, struct textloc);
struct textloc	_mdl_textloc_zero(void);
__END_DECLS

#endif /* !MDL_TEXTLOC_H */
