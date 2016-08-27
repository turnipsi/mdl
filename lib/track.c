/* $Id: track.c,v 1.10 2016/08/27 20:41:31 je Exp $ */

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
#include "midi.h"
#include "track.h"

struct track *
_mdl_track_new(enum instrument_type type, const char *trackname)
{
	struct track *track;
	char *default_instrument;

	assert(type == INSTR_DRUMKIT || type == INSTR_TONED);

	if ((track = malloc(sizeof(struct track))) == NULL) {
		warn("malloc failure in _mdl_track_new");
		return NULL;
	}

	if ((track->name = strdup(trackname)) == NULL) {
		warn("strdup in _mdl_track_new");
		free(track);
		return NULL;
	}

	if (type == INSTR_DRUMKIT) {
		track->autoallocate_channel = 0;
		track->midichannel = MIDI_DRUMCHANNEL;
	} else {
		track->autoallocate_channel = 1;
		track->midichannel = -1;
	}

	track->instrument = _mdl_get_instrument(type, trackname);
	if (track->instrument == NULL) {
		if (type == INSTR_TONED) {
			default_instrument = DEFAULT_TONED_INSTRUMENT;
		} else {
			default_instrument = DEFAULT_DRUMKIT;
		}
		track->instrument = _mdl_get_instrument(type,
		    default_instrument);
	}
	assert(track->instrument != NULL);

	track->volume = TRACK_DEFAULT_VOLUME;

	return track;
}
