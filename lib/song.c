/* $Id: song.c,v 1.8 2016/03/26 20:20:49 je Exp $ */

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

static int
connect_tracks_to_song(struct song *, struct musicexpr *, int);

struct song *
mdl_song_new(struct musicexpr *me, int level)
{
	struct song *song;
	struct track *track;

	if ((song = malloc(sizeof(struct song))) == NULL) {
		warn("malloc failure in mdl_new_song");
		return NULL;
	}

	SLIST_INIT(&song->tracklist);

	if (connect_tracks_to_song(song, me, level) != 0) {
		free(song);
		return NULL;
	}

	track = mdl_song_find_track_or_new(song, "acoustic grand", level);
	if (track == NULL) {
		warnx("could not create the default track");
		free(song);
		return NULL;
	}

	song->default_track = track;

	return song;
}

static int
connect_tracks_to_song(struct song *song, struct musicexpr *me, int level)
{
	struct musicexpr *p;
	struct track *tmp_track, *track;
	int ret;

	/*
	 * XXX Use some higher-order subroutine to simplify functions like
	 * XXX this one?
	 */

	ret = 0;

	level += 1;

	switch (me->me_type) {
	case ME_TYPE_ABSNOTE:
	case ME_TYPE_EMPTY:
	case ME_TYPE_RELNOTE:
	case ME_TYPE_REST:
		break;
	case ME_TYPE_CHORD:
		ret = connect_tracks_to_song(song, me->u.chord.me, level);
		break;
	case ME_TYPE_JOINEXPR:
		ret = connect_tracks_to_song(song, me->u.joinexpr.a, level);
		if (ret != 0)
			break;
		ret = connect_tracks_to_song(song, me->u.joinexpr.b, level);
		break;
	case ME_TYPE_NOTEOFFSETEXPR:
		ret = connect_tracks_to_song(song, me->u.noteoffsetexpr.me,
		    level);
		break;
	case ME_TYPE_OFFSETEXPR:
		ret = connect_tracks_to_song(song, me->u.offsetexpr.me, level);
		break;
	case ME_TYPE_ONTRACK:
		tmp_track = me->u.ontrack.track;
		track = mdl_song_find_track_or_new(song, tmp_track->name,
		    level);
		if (track == NULL) {
			ret = 1;
			break;
		}
		me->u.ontrack.track = track;
		free(tmp_track->name);
		free(tmp_track);
		ret = connect_tracks_to_song(song, me->u.ontrack.me, level);
		break;
	case ME_TYPE_RELSIMULTENCE:
	case ME_TYPE_SCALEDEXPR:
		ret = connect_tracks_to_song(song, me->u.scaledexpr.me, level);
		break;
	case ME_TYPE_SEQUENCE:
	case ME_TYPE_SIMULTENCE:
		TAILQ_FOREACH(p, &me->u.melist, tq) {
			ret = connect_tracks_to_song(song, p, level);
			if (ret != 0)
				break;
		}
		break;
	default:
		assert(0);
	}

	return ret;
}

void
mdl_song_free(struct song *song)
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
mdl_song_find_track_or_new(struct song *song, char *trackname, int level)
{
	struct track *track;

	/* XXX Drum track should be treated in a special way. */

	SLIST_FOREACH(track, &song->tracklist, sl)
		if (strcmp(track->name, trackname) == 0)
			return track;

	if ((track = malloc(sizeof(struct track))) == NULL) {
		warn("malloc failure in mdl_song_find_track_or_new");
		return NULL;
	}

	if ((track->name = strdup(trackname)) == NULL) {
		warn("strdup in mdl_song_find_track_or_new");
		free(track);
		return NULL;
	}

	SLIST_INSERT_HEAD(&song->tracklist, track, sl);

	mdl_log(MDLLOG_SONG, level, "added a new track \"%s\"\n", track->name);

	return track;
}
