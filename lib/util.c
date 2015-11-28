/* $Id: util.c,v 1.12 2015/11/28 18:03:18 je Exp $ */

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

#include <assert.h>
#include <err.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "util.h"

#define DEFAULT_SLOTCOUNT 1024

extern char *mdl_process_type;
extern char *__progname;

int loglevel = 0;

void
mdl_log(int msgloglevel, int indentlevel, const char *fmt, ...)
{
	va_list va;
	int ret, i;

	assert(msgloglevel >= 0);
	assert(indentlevel >= 0);

	if (msgloglevel > loglevel)
		return;

	ret = printf("%s/%s(%d): ",
		     __progname,
		     mdl_process_type,
		     msgloglevel);
	if (ret < 0)
		return;

	for (i = 0; i < indentlevel; i++) {
		ret = printf("  ");
		if (ret < 0)
			return;
	}

	va_start(va, fmt);
	(void) vprintf(fmt, va);
	va_end(va);
}

void *
mdl_stream_init(struct streamparams *params, size_t itemsize)
{
	void *items;

	params->count = 0;
	params->itemsize = itemsize;
	params->slotcount = 0;

	items = calloc(DEFAULT_SLOTCOUNT, params->itemsize);
	if (items == NULL) {
		warn("malloc failure in mdl_stream_init");
		return NULL;
	}

	params->slotcount = DEFAULT_SLOTCOUNT;

	return items;
}

int
mdl_stream_increment(struct streamparams *params, void **items)
{
	void *new_items;

	params->count += 1;
	if (params->count == params->slotcount) {
		mdl_log(2,
			0,
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

void
mdl_stream_free(struct streamparams *params, void **items)
{
	free(*items);
	*items = NULL;

	params->count = 0;
	params->slotcount = 0;
}
