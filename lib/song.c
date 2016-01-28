/* $Id: song.c,v 1.1 2016/01/28 21:18:11 je Exp $ */

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

#include "midi.h"
#include "musicexpr.h"
#include "song.h"
#include "util.h"

struct song_t *
mdl_song_new(int level)
{
	struct song_t *song;
	struct track_t *track;

	if ((song = malloc(sizeof(struct song_t))) == NULL) {
		warn("malloc failure in mdl_new_song");
		return NULL;
	}

	song->trackcount = 0;

	/* XXX is this a hack? */
	track = mdl_song_find_track_or_new(song, "acoustic grand", level);
	if (track == NULL) {
		warnx("could not create the default track");
		mdl_song_free(song);
		return NULL;
	}

	return song;
}

void
mdl_song_free(struct song_t *song)
{
	int i;

	for (i = 0; i < song->trackcount; i++)
		free(song->tracks[i].trackname);

	free(song);
}

struct track_t *
mdl_song_get_default_track(struct song_t *song)
{
	assert(song->trackcount > 0);

	return &song->tracks[0];
}

struct track_t *
mdl_song_find_track_or_new(struct song_t *song, char *trackname, int level)
{
	struct track_t *track;
	struct instrument_t *instrument;
	char *trackname_copy;
	int i;

	/* XXX drum track should be treated in a special way */

	for (i = 0; i < song->trackcount; i++)
		if (strcmp(song->tracks[i].trackname, trackname) == 0)
			return &song->tracks[i];

	if (song->trackcount == MIDI_CHANNEL_MAX + 1) {
		warnx("maximum track count reached, ignoring track %s",
		      trackname);
		return NULL;
	}

	if ((trackname_copy = strdup(trackname)) == NULL) {
		warn("strdup in mdl_song_find_track_or_new");
		return NULL;
	}

	track = &song->tracks[ song->trackcount ];
	track->trackname = trackname_copy;

	instrument = get_instrument(INSTR_TONED, track->trackname);
	if (instrument == NULL) {
		instrument = get_instrument(INSTR_TONED, "acoustic grand");
		assert(instrument != NULL);
	}
	track->instrument = instrument;

	song->trackcount += 1;

	mdl_log(2, level, "added a new track %s\n", track->trackname);

	return track;
}
