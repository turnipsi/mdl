/* $Id: song.c,v 1.2 2016/01/29 20:51:26 je Exp $ */

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

	SLIST_INIT(&song->tracklist);

	track = mdl_song_find_track_or_new(song, "acoustic grand", level);
	if (track == NULL) {
		warnx("could not create the default track");
		free(song);
		return NULL;
	}

	song->default_track = track;

	return song;
}

void
mdl_song_free(struct song_t *song)
{
	struct track_t *p, *q;

	SLIST_FOREACH_SAFE(p, &song->tracklist, sl, q) {
		SLIST_REMOVE(&song->tracklist, p, track_t, sl);
		free(p->trackname);
		free(p);
	}

	free(song);
}

struct track_t *
mdl_song_find_track_or_new(struct song_t *song, char *trackname, int level)
{
	struct track_t *track;

	/* XXX drum track should be treated in a special way */

	SLIST_FOREACH(track, &song->tracklist, sl)
		if (strcmp(track->trackname, trackname) == 0)
			return track;

	if ((track = malloc(sizeof(struct track_t))) == NULL) {
		warn("malloc failure in mdl_song_find_track_or_new");
		return NULL;
	}

	if ((track->trackname = strdup(trackname)) == NULL) {
		warn("strdup in mdl_song_find_track_or_new");
		free(track);
		return NULL;
	}

	SLIST_INSERT_HEAD(&song->tracklist, track, sl);

	mdl_log(2, level, "added a new track %s\n", track->trackname);

	return track;
}
