/* $Id: song.c,v 1.18 2016/08/23 20:22:58 je Exp $ */

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
#include "musicexpr.h"
#include "song.h"
#include "util.h"

static int
connect_tracks_to_song(struct song *, struct musicexpr *, int);

struct song *
_mdl_song_new(void)
{
	struct song *song;

	if ((song = malloc(sizeof(struct song))) == NULL) {
		warn("malloc failure in mdl_new_song");
		return NULL;
	}

	SLIST_INIT(&song->tracklist);

	return song;
}

int
_mdl_song_setup_tracks(struct song *song, struct musicexpr *me, int level)
{
	struct track *track;

	if (connect_tracks_to_song(song, me, level) != 0) {
		warnx("could not connect tracks to song");
		return 1;
	}

	track = _mdl_song_find_track_or_new(song, INSTR_TONED,
	    DEFAULT_TONED_INSTRUMENT, level);
	if (track == NULL) {
		warnx("could not create the default toned instrument track");
		return 1;
	}
	song->default_tonedtrack = track;

	track = _mdl_song_find_track_or_new(song, INSTR_DRUMKIT,
	    DEFAULT_DRUMKIT, level);
	if (track == NULL) {
		warnx("could not create the default drumkit track");
		return 1;
	}
	song->default_drumtrack = track;

	return 0;
}

static int
connect_tracks_to_song(struct song *song, struct musicexpr *me, int level)
{
	struct musicexpr *p;
	struct musicexpr_iter iter;
	struct track *tmp_track, *track;
	int ret;

	assert(me->me_type != ME_TYPE_FUNCTION);

	ret = 0;
	level += 1;

	if (me->me_type == ME_TYPE_ONTRACK) {
		tmp_track = me->u.ontrack.track;
		track = _mdl_song_find_track_or_new(song,
		    tmp_track->instrument->type, tmp_track->name, level);
		if (track == NULL)
			return 1;
		me->u.ontrack.track = track;
		free(tmp_track->name);
		free(tmp_track);
		return connect_tracks_to_song(song, me->u.ontrack.me, level);
	}

	/* Traverse the subexpressions. */
	iter = _mdl_musicexpr_iter_new(me);
	while ((p = _mdl_musicexpr_iter_next(&iter)) != NULL) {
		if ((ret = connect_tracks_to_song(song, p, level)) != 0)
			return ret;
	}

	return 0;
}

void
_mdl_song_free(struct song *song)
{
	struct track *p, *q;

	SLIST_FOREACH_SAFE(p, &song->tracklist, sl, q) {
		SLIST_REMOVE(&song->tracklist, p, track, sl);
		free(p->name);
		free(p);
	}

	free(song);
}

struct track *
_mdl_song_find_track_or_new(struct song *song, enum instrument_type instr_type,
    char *trackname, int level)
{
	struct track *track;

	SLIST_FOREACH(track, &song->tracklist, sl)
		if (strcmp(track->name, trackname) == 0 &&
		    track->instrument->type == instr_type)
			return track;

	track = _mdl_track_new(instr_type, trackname);
	if (track == NULL) {
		warnx("error in creating a new track");
		return NULL;
	}

	SLIST_INSERT_HEAD(&song->tracklist, track, sl);

	_mdl_log(MDLLOG_SONG, level, "added a new track \"%s\"\n",
	    track->name);

	return track;
}
