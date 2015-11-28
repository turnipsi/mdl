/* $Id: util.c,v 1.9 2015/11/28 08:46:23 je Exp $ */

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

#include <err.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "util.h"

#define DEFAULT_SLOTCOUNT 1024

int debuglevel = 0;

int
mdl_log(int loglevel, const char *fmt, ...)
{
	va_list va;
	int ret;

	if (loglevel > debuglevel)
		return 0;

	va_start(va, fmt);
	ret = vprintf(fmt, va);
	va_end(va);

	return ret;
}

void *
mdl_init_stream(struct streamparams *params, size_t itemsize)
{
	void *items;

	params->count = 0;
	params->itemsize = itemsize;
	params->slotcount = DEFAULT_SLOTCOUNT;

	items = calloc(params->slotcount, params->itemsize);
	if (items == NULL) {
		warn("malloc failure in mdl_init_stream");
		return NULL;
	}

	return items;
}

int
mdl_increment_stream(struct streamparams *params, void **items)
{
	void *new_items;

	params->count += 1;
	if (params->count == params->slotcount) {
		(void) mdl_log(2,
			       "mdl_buffer now contains %d items\n",
			       params->count);
		params->slotcount *= 2;
		new_items = reallocarray(*items,
					 params->slotcount,
					 params->itemsize);
		if (new_items == NULL) {
			warn("reallocarray in mdl_increment_buffer");
			return 1;
		}
		*items = new_items;
	}

	return 0;
}
