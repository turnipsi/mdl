/* $Id: track.c,v 1.6 2016/08/22 20:18:48 je Exp $ */

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
#include <stdlib.h>
#include <string.h>

#include "instrument.h"
#include "track.h"

struct instrument *
_mdl_track_get_default_instrument(enum instrument_type type,
    struct track *track)
{
	return _mdl_get_instrument(type, track->name);
}

struct track *
_mdl_track_new(const char *trackname)
{
	struct track *track;

	if ((track = malloc(sizeof(struct track))) == NULL) {
		warn("malloc failure in _mdl_track_new");
		return NULL;
	}

	if ((track->name = strdup(trackname)) == NULL) {
		warn("strdup in _mdl_track_new");
		free(track);
		return NULL;
	}

	track->prev_midich = -1;
	track->volume = TRACK_DEFAULT_VOLUME;

	return track;
}
