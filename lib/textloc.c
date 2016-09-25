/* $Id: textloc.c,v 1.2 2016/09/25 15:41:54 je Exp $ */

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

#include <stdarg.h>

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

struct textloc
_mdl_join_textlocs(struct textloc *first, ...)
{
	va_list va;
	struct textloc result;
	struct textloc *current;

	if (first == NULL)
		return _mdl_textloc_zero();

	result = *first;

	va_start(va, first);

	current = va_arg(va, struct textloc *);
	while (current != NULL) {
		if (current->first_line != 0) {
			if (current->first_line < result.first_line) {
				result.first_line   = current->first_line;
				result.first_column = current->first_column;
			} else if (current->first_line == result.first_line) {
				result.first_column = MIN(result.first_column,
				    current->first_column);
			}
		}

		if (current->last_line != 0) {
			if (current->last_line > result.last_line) {
				result.last_line   = current->last_line;
				result.last_column = current->last_column;
			} else if (current->last_line == result.last_line) {
				result.last_column = MAX(result.last_column,
				    current->last_column);
			}
		}

		current = va_arg(va, struct textloc *);
	}

	va_end(va);

	return result;
}
